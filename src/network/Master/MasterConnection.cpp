#include "Common.h"
#include "SystemUtils.h"
#include "KvmEvents.h"
#include "KvmContext.h"
#include "MasterInternal.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QMetaObject> 
#include <vector>
#include <thread>
#include <chrono>

unsigned long long StrToBthAddr(const std::string& str) {
    unsigned long long addr = 0;
    int shift = 0;
    for (int i = str.length() - 1; i >= 0; i--) {
        char c = str[i];
        if (c >= '0' && c <= '9') addr |= ((unsigned long long)(c - '0')) << shift;
        else if (c >= 'A' && c <= 'F') addr |= ((unsigned long long)(c - 'A' + 10)) << shift;
        else if (c >= 'a' && c <= 'f') addr |= ((unsigned long long)(c - 'a' + 10)) << shift;
        else continue;
        shift += 4;
    }
    return addr;
}

void NetworkMonitorThreadFunc(std::shared_ptr<SlaveCtx> ctx) {
    if (!ctx->isBluetooth) return; 
    bool lastHasNet = SystemUtils::HasNetworkConnectivity();
    std::string lastNetName = SystemUtils::GetActiveNetworkName();
    
    while (g_Running && ctx->connected && ctx->sock != INVALID_SOCKET_HANDLE) {
        bool currentHasNet = SystemUtils::HasNetworkConnectivity();
        std::string currentNetName = SystemUtils::GetActiveNetworkName();
        
        if (lastHasNet && !currentHasNet) {
            char pkt = 32;
            {
                std::lock_guard<std::mutex> lock(ctx->sendLock);
                send(ctx->sock, &pkt, 1, 0);
            }
            DebugLog("[MASTER] Network Lost! Sending Flag 32 and fallback to BT.\n");
            ctx->tcpFileFailed = true;
            if (ctx->fileSock != INVALID_SOCKET_HANDLE && ctx->fileSock != ctx->btFileSock) {
                NetUtils::CloseSocket(ctx->fileSock);
                ctx->fileSock = ctx->btFileSock;
                {
                    std::lock_guard<std::mutex> tLock(g_TaskMutex);
                    for (auto& kv : g_TransferTasks) {
                        if (!kv.second->paused.exchange(true)) {
                            if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new TransferPauseEvent(kv.first, true));
                        }
                    }
                }
            } else {
                ctx->fileSock = ctx->btFileSock;
            }
        } else if ((!lastHasNet && currentHasNet) || (currentHasNet && currentNetName != lastNetName && !currentNetName.empty() && !lastNetName.empty())) {
            char pkt = 20;
            {
                std::lock_guard<std::mutex> lock(ctx->sendLock);
                send(ctx->sock, &pkt, 1, 0);
            }
            DebugLog("[MASTER] Network Recovered/Changed! Sending Flag 20.\n");
        }
        
        lastHasNet = currentHasNet;
        if (!currentNetName.empty()) lastNetName = currentNetName;
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

// Global routing map
struct RelayTaskInfo {
    std::weak_ptr<SlaveCtx> source;
    std::weak_ptr<SlaveCtx> dest;
    uint32_t destTaskId;
    uint32_t sourceTaskId;
};
std::map<uint32_t, RelayTaskInfo> g_RelayBySourceId; 
std::map<std::pair<void*, uint32_t>, uint32_t> g_RelayByDestId; 
std::mutex g_RelayMutex;
std::atomic<uint32_t> g_MasterRelayTaskIdx(100000);

void ReceiverThreadFunc(std::shared_ptr<SlaveCtx> ctx) {
    std::vector<char> buffer(1024 * 128);
    int offset = 0;

    while (g_Running && ctx->connected && ctx->sock != INVALID_SOCKET_HANDLE) {
        int ret = recv(ctx->sock, buffer.data() + offset, buffer.size() - offset, 0);
        if (ret <= 0) break;
        int total = offset + ret;
        int processed = 0;

        while (processed < total) {
            char* ptr = buffer.data() + processed;
            int remaining = total - processed;
            if (remaining < 1) break;

            char flag = ptr[0];

            if (flag == 3) { 
                if (remaining < 5) break;
                unsigned int nLen; memcpy(&nLen, ptr + 1, 4);
                int len = ntohl(nLen);
                if (len < 0 || remaining < 5 + len) break;
                std::string received_text(ptr + 5, len);
                if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new ClipboardEvent(received_text));
                processed += (5 + len);
            } else if (flag == 5) { 
                if (remaining < 5) break;
                unsigned int netTime; memcpy(&netTime, ptr + 1, 4);
                uint32_t sentTime = ntohl(netTime);
                ctx->latency = SystemUtils::GetTimeMS() - sentTime;
                
                char latPkt[5];
                latPkt[0] = 26;
                unsigned int nLat = htonl(ctx->latency.load());
                memcpy(latPkt + 1, &nLat, 4);
                {
                    std::lock_guard<std::mutex> lock(ctx->sendLock);
                    send(ctx->sock, latPkt, 5, 0);
                }
                
                processed += 5;
            } else if (flag == 8) { 
                ctx->connected = false;
                break; 
            } else if (flag == 12) { 
                if (remaining < 5) break;
                unsigned int numFiles; memcpy(&numFiles, ptr+1, 4);
                int fileCount = ntohl(numFiles);
                DebugLog("[MASTER] Received Root File Info (Flag 12) from Slave: %d roots\n", fileCount);
                
                {
                    std::lock_guard<std::mutex> lock(g_FileClipMutex);
                    g_ReceivedRoots.clear(); 
                    
                    int currentOffset = 5;
                    std::vector<FileClipInfo> infos;
                    bool safe = true;
                    for(int i=0; i<fileCount; ++i) {
                        if (remaining < currentOffset + 4) {safe = false; break;}
                        unsigned int nameLen; memcpy(&nameLen, ptr+currentOffset, 4);
                        int len = ntohl(nameLen);
                        currentOffset += 4;
                        if (len < 0 || remaining < currentOffset + len + 8) {safe = false; break;}
                        std::string name(ptr+currentOffset, len);
                        currentOffset += len;
                        uint64_t netSize; memcpy(&netSize, ptr+currentOffset, 8);
                        currentOffset += 8;
                        infos.push_back({name, ntohll(netSize), ""}); 
                        g_ReceivedRoots.push_back(name); 
                        DebugLog("[MASTER] Root[%d]: %s\n", i, name.c_str());
                    }
                    if (safe) {
                        g_LastCopiedFiles = infos; 
                        g_RemoteFilesAvailable = true;
                        g_HasFileUpdate = true; 
                        g_RemoteFileSource = ctx; 
                        processed += currentOffset;
                    } else break;
                }
            } else if (flag == 16) { 
                int p = 1;
                if (remaining < p + 4) break;
                unsigned int nTaskId; memcpy(&nTaskId, ptr + p, 4); p += 4;
                uint32_t taskId = ntohl(nTaskId);
                
                if (remaining < p + 4) break;
                unsigned int nDevLen; memcpy(&nDevLen, ptr + p, 4); p += 4;
                int devLen = ntohl(nDevLen);
                if (devLen < 0 || remaining < p + devLen) break;
                std::string deviceName(ptr + p, devLen); p += devLen;
                
                if (remaining < p + 4) break;
                unsigned int nPathLen; memcpy(&nPathLen, ptr + p, 4); p += 4;
                int pathLen = ntohl(nPathLen);
                if (pathLen < 0 || remaining < p + pathLen) break;
                std::string targetPath(ptr + p, pathLen); p += pathLen;

                int connectedSlaves = 0;
                {
                    std::lock_guard<std::mutex> lk(g_SlaveListLock);
                    for(auto& s : g_SlaveList) if(s->connected) connectedSlaves++;
                }
                if (connectedSlaves > 1) {
                    std::lock_guard<std::mutex> tLock(g_TaskMutex);
                    if (!g_TransferTasks.empty()) {
                        char cancelPkt[5];
                        cancelPkt[0] = 19;
                        unsigned int nT = htonl(taskId);
                        memcpy(cancelPkt + 1, &nT, 4);
                        std::lock_guard<std::mutex> sendLock(ctx->sendLock);
                        send(ctx->sock, cancelPkt, 5, 0);
                        processed += p;
                        continue;
                    }
                }
                
                if (g_RemoteFilesAvailable) {
                    auto srcCtx = g_RemoteFileSource.lock();
                    if (srcCtx && srcCtx != ctx) {
                        uint32_t sourceTaskId = ++g_MasterRelayTaskIdx;
                        {
                            std::lock_guard<std::mutex> rLock(g_RelayMutex);
                            g_RelayBySourceId[sourceTaskId] = { srcCtx, ctx, taskId, sourceTaskId };
                            g_RelayByDestId[{ctx.get(), taskId}] = sourceTaskId;
                        }
                        
                        std::vector<char> relayData;
                        relayData.push_back(16);
                        unsigned int nSrcId = htonl(sourceTaskId);
                        relayData.insert(relayData.end(), (char*)&nSrcId, (char*)&nSrcId + 4);
                        
                        unsigned int nNameLen = htonl(ctx->name.length());
                        relayData.insert(relayData.end(), (char*)&nNameLen, (char*)&nNameLen + 4);
                        relayData.insert(relayData.end(), ctx->name.begin(), ctx->name.end());
                        
                        unsigned int nPathL = htonl(targetPath.length());
                        relayData.insert(relayData.end(), (char*)&nPathL, (char*)&nPathL + 4);
                        relayData.insert(relayData.end(), targetPath.begin(), targetPath.end());

                        if (srcCtx->fileSock != INVALID_SOCKET_HANDLE) {
                            std::thread([srcCtx, relayData]() {
                                std::lock_guard<std::mutex> sendLock(srcCtx->fileSendLock);
                                NetUtils::SendAll(srcCtx->fileSock, relayData.data(), relayData.size());
                            }).detach();
                        }
                        processed += p;
                        continue;
                    }
                }

                processed += p;

                auto task = std::make_shared<FileTransferTask>();
                task->taskId = taskId;
                task->isSender = true;
                task->deviceName = ctx->name; 
                task->targetPath = targetPath;
                {
                    std::lock_guard<std::mutex> lock(g_TaskMutex);
                    g_TransferTasks[taskId] = task;
                }

                std::vector<FileClipInfo> localRoots;
                {
                    std::lock_guard<std::mutex> lock(g_FileClipMutex);
                    localRoots = g_LastCopiedFiles;
                }

                std::thread(MasterSendFileThread, ctx, taskId, localRoots).detach();
            } else if (flag == 21) { 
                if (remaining < 7) break;
                unsigned int nCount; memcpy(&nCount, ptr + 1, 4);
                int count = ntohl(nCount);
                unsigned short nPort; memcpy(&nPort, ptr + 5, 2);
                int port = ntohs(nPort);
                
                int currentOffset = 7;
                std::vector<std::string> ips;
                bool safe = true;
                for(int i=0; i<count; ++i) {
                     if (remaining < currentOffset + 4) { safe = false; break; }
                     unsigned int nLen; memcpy(&nLen, ptr + currentOffset, 4);
                     int len = ntohl(nLen);
                     currentOffset += 4;
                     if (len < 0 || remaining < currentOffset + len) { safe = false; break; }
                     ips.push_back(std::string(ptr + currentOffset, len));
                     currentOffset += len;
                }

                if (safe) {
                    if (count == 0 || !SystemUtils::HasNetworkConnectivity()) {
                        DebugLog("[MASTER] Network check failed. Aborting TCP File connect.\n");
                        ctx->tcpFileFailed = true;
                    } else {
                        DebugLog("[MASTER] Received IP List (%d IPs) on Port %d. Racing...\n", count, port);
                        std::thread([=](std::vector<std::string> targetIPs, int targetPort, std::shared_ptr<SlaveCtx> sCtx){
                            SOCKET sock = INVALID_SOCKET;
                            for(const auto& ip : targetIPs) {
                                DebugLog("[MASTER] Trying %s:%d...\n", ip.c_str(), targetPort);
                                sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                                if (sock == INVALID_SOCKET) continue;
                                
                                SOCKADDR_IN addr = {0};
                                inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
                                addr.sin_family = AF_INET;
                                addr.sin_port = htons(targetPort);
                                
                                if (connect(sock, (SOCKADDR*)&addr, sizeof(addr)) == 0) {
                                    DebugLog("[MASTER] Connected to %s!\n", ip.c_str());
                                    int one = 1;
                                    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one));
                                    int keepAlive = 1;
                                    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char*)&keepAlive, sizeof(keepAlive));
                                    
                                    SocketHandle oldFileSock = sCtx->fileSock;
                                    sCtx->fileSock = (SocketHandle)sock;
                                    sCtx->tcpFileFailed = false;
                                    
                                    if (oldFileSock != INVALID_SOCKET_HANDLE && oldFileSock != sCtx->btFileSock) {
                                        NetUtils::CloseSocket(oldFileSock); 
                                    }
                                    
                                    std::thread(FileReceiverThreadFunc, sCtx).detach(); 
                                    
                                    std::thread([sCtx]() {
                                        sCtx->lastFilePongTime = SystemUtils::GetTimeMS();
                                        SocketHandle mySock = sCtx->fileSock;
                                        while (g_Running && sCtx->connected && sCtx->fileSock == mySock && mySock != INVALID_SOCKET_HANDLE) {
                                            char ping[1] = { 30 };
                                            bool sendFailed = false;
                                            {
                                                std::lock_guard<std::mutex> lock(sCtx->fileSendLock);
                                                if (sCtx->fileSock != mySock) break; 
                                                if (!NetUtils::SendAll(mySock, ping, 1)) {
                                                    sendFailed = true;
                                                }
                                            }
                                            if (sendFailed) {
                                                if (sCtx->fileSock == mySock) {
                                                    sCtx->tcpFileFailed = true;
                                                    if (mySock != sCtx->btFileSock) {
                                                        NetUtils::CloseSocket(mySock);
                                                        sCtx->fileSock = sCtx->btFileSock;
                                                        {
                                                            std::lock_guard<std::mutex> tLock(g_TaskMutex);
                                                            for (auto& kv : g_TransferTasks) {
                                                                if (!kv.second->paused.exchange(true)) {
                                                                    if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new TransferPauseEvent(kv.first, true));
                                                                }
                                                            }
                                                        }
                                                    } else {
                                                        NetUtils::CloseSocket(mySock);
                                                    }
                                                }
                                                break;
                                            }
                                            int timeout = (mySock != sCtx->btFileSock) ? 15000 : 30000;
                                            if (SystemUtils::GetTimeMS() - sCtx->lastFilePongTime.load() > timeout) {
                                                DebugLog("[MASTER] File TCP Heartbeat timeout!\n");
                                                if (sCtx->fileSock == mySock) {
                                                    sCtx->tcpFileFailed = true;
                                                    if (mySock != sCtx->btFileSock) {
                                                        NetUtils::CloseSocket(mySock);
                                                        sCtx->fileSock = sCtx->btFileSock;
                                                        {
                                                            std::lock_guard<std::mutex> tLock(g_TaskMutex);
                                                            for (auto& kv : g_TransferTasks) {
                                                                if (!kv.second->paused.exchange(true)) {
                                                                    if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new TransferPauseEvent(kv.first, true));
                                                                }
                                                            }
                                                        }
                                                    } else {
                                                        NetUtils::CloseSocket(mySock);
                                                    }
                                                }
                                                break;
                                            }
                                            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                                        }
                                    }).detach();
                                    
                                    return;
                                }
                                closesocket(sock);
                            }
                            DebugLog("[MASTER] Failed to connect to any IP.\n");
                            sCtx->tcpFileFailed = true; 
                        }, ips, port, ctx).detach();
                    }
                    processed += currentOffset;
                } else break;
            } else if (flag == 19) { 
                if (remaining < 5) break;
                unsigned int nTaskId; memcpy(&nTaskId, ptr + 1, 4);
                uint32_t taskId = ntohl(nTaskId);
                DebugLog("[DEBUG-CANCEL] Network Received Flag 19 (Cancel) for Task %u\n", taskId);
                
                bool isRelay = false;
                {
                    std::lock_guard<std::mutex> rLock(g_RelayMutex);
                    auto itDst = g_RelayByDestId.find({ctx.get(), taskId});
                    if (itDst != g_RelayByDestId.end()) {
                        uint32_t srcTaskId = itDst->second;
                        auto fwd = g_RelayBySourceId[srcTaskId].source.lock();
                        if (fwd) {
                            unsigned int nNewId = htonl(srcTaskId);
                            std::vector<char> relayData(ptr, ptr + 5);
                            memcpy(relayData.data() + 1, &nNewId, 4);
                            std::thread([fwd, relayData]() {
                                std::lock_guard<std::mutex> sendLock(fwd->sendLock);
                                send(fwd->sock, relayData.data(), 5, 0);
                            }).detach();
                        }
                        isRelay = true;
                    } else {
                        auto itSrc = g_RelayBySourceId.find(taskId);
                        if (itSrc != g_RelayBySourceId.end() && itSrc->second.source.lock() == ctx) {
                            uint32_t dstTaskId = itSrc->second.destTaskId;
                            auto fwd = itSrc->second.dest.lock();
                            if (fwd) {
                                unsigned int nNewId = htonl(dstTaskId);
                                std::vector<char> relayData(ptr, ptr + 5);
                                memcpy(relayData.data() + 1, &nNewId, 4);
                                std::thread([fwd, relayData]() {
                                    std::lock_guard<std::mutex> sendLock(fwd->sendLock);
                                    send(fwd->sock, relayData.data(), 5, 0);
                                }).detach();
                            }
                            isRelay = true;
                        }
                    }
                }
                if (isRelay) { processed += 5; continue; }

                std::shared_ptr<FileTransferTask> task;
                {
                    std::lock_guard<std::mutex> lock(g_TaskMutex);
                    if (g_TransferTasks.count(taskId)) {
                        task = g_TransferTasks[taskId];
                        task->cancelled = true;
                        task->receivingFiles.clear();
                        if (g_Context->FileLockMgr) {
                            for (const auto& kv : task->tempFilePaths) {
                                g_Context->FileLockMgr->UnlockPath(kv.second);
                            }
                        }
                        task->tempFilePaths.clear();
                    }
                }
                if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new FileTransferCancelEvent(taskId));
                processed += 5;
            } else if (flag == 22) { 
                if (remaining < 6) break;
                unsigned int nTaskId; memcpy(&nTaskId, ptr + 1, 4);
                uint32_t taskId = ntohl(nTaskId);
                bool paused = (ptr[5] == 1);
                DebugLog("[DEBUG-PAUSE] Network Received Flag 22 (Pause) for Task %u (Paused: %d)\n", taskId, paused);
                
                bool isRelay = false;
                {
                    std::lock_guard<std::mutex> rLock(g_RelayMutex);
                    auto itDst = g_RelayByDestId.find({ctx.get(), taskId});
                    if (itDst != g_RelayByDestId.end()) {
                        uint32_t srcTaskId = itDst->second;
                        auto fwd = g_RelayBySourceId[srcTaskId].source.lock();
                        if (fwd) {
                            unsigned int nNewId = htonl(srcTaskId);
                            std::vector<char> relayData(ptr, ptr + 6);
                            memcpy(relayData.data() + 1, &nNewId, 4);
                            std::thread([fwd, relayData]() {
                                std::lock_guard<std::mutex> sendLock(fwd->sendLock);
                                send(fwd->sock, relayData.data(), 6, 0);
                            }).detach();
                        }
                        isRelay = true;
                    } else {
                        auto itSrc = g_RelayBySourceId.find(taskId);
                        if (itSrc != g_RelayBySourceId.end() && itSrc->second.source.lock() == ctx) {
                            uint32_t dstTaskId = itSrc->second.destTaskId;
                            auto fwd = itSrc->second.dest.lock();
                            if (fwd) {
                                unsigned int nNewId = htonl(dstTaskId);
                                std::vector<char> relayData(ptr, ptr + 6);
                                memcpy(relayData.data() + 1, &nNewId, 4);
                                std::thread([fwd, relayData]() {
                                    std::lock_guard<std::mutex> sendLock(fwd->sendLock);
                                    send(fwd->sock, relayData.data(), 6, 0);
                                }).detach();
                            }
                            isRelay = true;
                        }
                    }
                }
                if (isRelay) { processed += 6; continue; }

                std::shared_ptr<FileTransferTask> task;
                {
                    std::lock_guard<std::mutex> lock(g_TaskMutex);
                    if (g_TransferTasks.count(taskId)) task = g_TransferTasks[taskId];
                }
                if (task) task->paused = paused;
                if (g_MainObject) {
                    QCoreApplication::postEvent(g_MainObject, new TransferPauseEvent(taskId, paused));
                }
                processed += 6;
            } else if (flag == 23) { 
                g_Locked = !g_Locked;
                if (g_MainObject) {
                    QMetaObject::invokeMethod(g_MainObject,[](){ UpdateUI(); });
                }
                processed += 1;
            } else if (flag == 25) { 
                if (remaining < 5) break;
                unsigned int pLen; memcpy(&pLen, ptr + 1, 4);
                int propsLen = ntohl(pLen);
                if (propsLen < 0 || remaining < 5 + propsLen) break;
                ctx->sysProps = std::string(ptr + 5, propsLen);
                processed += (5 + propsLen);
            } else if (flag == 32) { 
                DebugLog("[MASTER] Received Flag 32 (NET_LOST). Fallback to BT.\n");
                ctx->tcpFileFailed = true;
                if (ctx->fileSock != INVALID_SOCKET_HANDLE && ctx->fileSock != ctx->btFileSock) {
                    NetUtils::CloseSocket(ctx->fileSock);
                    ctx->fileSock = ctx->btFileSock;
                    {
                        std::lock_guard<std::mutex> tLock(g_TaskMutex);
                        for (auto& kv : g_TransferTasks) {
                            if (!kv.second->paused.exchange(true)) {
                                if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new TransferPauseEvent(kv.first, true));
                            }
                        }
                    }
                } else {
                    ctx->fileSock = ctx->btFileSock;
                }
                processed += 1;
            } else if (flag == 33) { 
                DebugLog("[MASTER] Received Flag 33 (NET_CHANGED). Sending Flag 20 to reconnect.\n");
                char pkt = 20;
                std::lock_guard<std::mutex> lock(ctx->sendLock);
                send(ctx->sock, &pkt, 1, 0);
                processed += 1;
            } else if (flag == 28) { 
                if (remaining < 9) break;
                double sScale; memcpy(&sScale, ptr + 1, 8);
                ctx->scale = sScale;
                processed += 9;
            } else {
                processed++;
            }
        }
        if (ctx->connected == false) break;
        if (processed < total) {
            memmove(buffer.data(), buffer.data() + processed, total - processed);
            offset = total - processed;
            
            if (offset == buffer.size()) {
                buffer.resize(buffer.size() * 2);
            }
        } else offset = 0;
    }
    
    ctx->connected = false;
    if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new DisconnectedEvent());
}

void StartReceiverThread(std::shared_ptr<SlaveCtx> ctx) {
    std::thread(ReceiverThreadFunc, ctx).detach(); 
    std::thread(NetworkMonitorThreadFunc, ctx).detach(); 
    if (ctx->fileSock != INVALID_SOCKET_HANDLE) {
        std::thread(FileReceiverThreadFunc, ctx).detach();
        std::thread([ctx]() {
            ctx->lastFilePongTime = SystemUtils::GetTimeMS();
            SocketHandle mySock = ctx->fileSock;
            while (g_Running && ctx->connected && ctx->fileSock == mySock && mySock != INVALID_SOCKET_HANDLE) {
                char ping[1] = { 30 };
                bool sendFailed = false;
                {
                    std::lock_guard<std::mutex> lock(ctx->fileSendLock);
                    if (ctx->fileSock != mySock) break; 
                    if (!NetUtils::SendAll(mySock, ping, 1)) {
                        sendFailed = true;
                    }
                }
                if (sendFailed) {
                    if (ctx->fileSock == mySock) {
                        ctx->tcpFileFailed = true;
                        if (mySock != ctx->btFileSock) {
                            NetUtils::CloseSocket(mySock);
                            ctx->fileSock = ctx->btFileSock;
                            {
                                std::lock_guard<std::mutex> tLock(g_TaskMutex);
                                for (auto& kv : g_TransferTasks) {
                                    if (!kv.second->paused.exchange(true)) {
                                        if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new TransferPauseEvent(kv.first, true));
                                    }
                                }
                            }
                        } else {
                            NetUtils::CloseSocket(mySock);
                        }
                    }
                    break;
                }
                int timeout = (mySock != ctx->btFileSock) ? 15000 : 30000;
                if (SystemUtils::GetTimeMS() - ctx->lastFilePongTime.load() > timeout) {
                    DebugLog("[MASTER] File TCP Heartbeat timeout!\n");
                    if (ctx->fileSock == mySock) {
                        ctx->tcpFileFailed = true;
                        if (mySock != ctx->btFileSock) {
                            NetUtils::CloseSocket(mySock);
                            ctx->fileSock = ctx->btFileSock;
                            {
                                std::lock_guard<std::mutex> tLock(g_TaskMutex);
                                for (auto& kv : g_TransferTasks) {
                                    if (!kv.second->paused.exchange(true)) {
                                        if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new TransferPauseEvent(kv.first, true));
                                    }
                                }
                            }
                        } else {
                            NetUtils::CloseSocket(mySock);
                        }
                    }
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        }).detach();
    }
}

void LatencyThread(std::shared_ptr<SlaveCtx> ctx) {
    while (g_Running && ctx->connected && ctx->sock != INVALID_SOCKET_HANDLE) {
        char buffer[5] = { 4 }; 
        uint32_t time = SystemUtils::GetTimeMS();
        unsigned int netTime = htonl(time);
        memcpy(buffer + 1, &netTime, 4);
        {
            std::lock_guard<std::mutex> lock(ctx->sendLock);
            if(send(ctx->sock, buffer, 5, 0) == SOCKET_ERROR_CODE) {
                ctx->connected = false;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void StartLatencyThread(std::shared_ptr<SlaveCtx> ctx) {
    std::thread(LatencyThread, ctx).detach();
}