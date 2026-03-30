#include "Common.h"
#include "SystemUtils.h"
#include "KvmEvents.h"
#include "KvmContext.h"
#include "SlaveInternal.h"
#include <QtCore/QCoreApplication>
#include <vector>
#include <thread>
#include <mutex>

void NetworkReceiver() {
    std::vector<char> buffer(1024 * 1024 * 4);
    int offset = 0;

    while (g_Running && g_ClientSock != INVALID_SOCKET_HANDLE) {
        int ret = recv(g_ClientSock, buffer.data() + offset, buffer.size() - offset, 0);
        if (ret <= 0) {
            MDC_LOG_INFO(LogTag::NET, "NetworkReceiver loop exited recv ret: %d", ret);
            break;
        }
        int total = offset + ret;
        int processed = 0;

        while (processed < total) {
            char* ptr = buffer.data() + processed;
            int remaining = total - processed;
            if (remaining < 1) break;
            char flag = ptr[0];

            if (flag == 1) { 
                if (remaining < 9) break;
                unsigned int netX, netY; memcpy(&netX, ptr + 1, 4); memcpy(&netY, ptr + 5, 4);
                SystemUtils::SetCursorPos(ntohl(netX), ntohl(netY));
                processed += 9;
            } 
            else if (flag == 2) { 
                if (remaining < 13) break;
                unsigned int nT, nP1, nP2;
                memcpy(&nT, ptr + 1, 4); memcpy(&nP1, ptr + 5, 4); memcpy(&nP2, ptr + 9, 4);
                int type = ntohl(nT), p1 = ntohl(nP1), p2 = ntohl(nP2);
                if (g_Context->InputInjector) {
                    if (type == 1) g_Context->InputInjector->SendMouseClick(p1, p2 != 0);
                    else if (type == 2) g_Context->InputInjector->SendMouseScroll(p2);
                    else if (type == 3) g_Context->InputInjector->SendKey(p1, p2, true);
                    else if (type == 4) g_Context->InputInjector->SendKey(p1, p2, false);
                    else if (type == 7) {
                        bool focused = (p1 == 1);
                        g_SlaveFocused = focused;
                        if (g_MainObject) {
                            QMetaObject::invokeMethod(g_MainObject,[](){ UpdateUI(); });
                        }
                    } else if (type == 8) { 
                        MDC_LOG_INFO(LogTag::TRANS, "Paste event Flag 8 request manifest Flag 16");
                        
                        std::string targetPath = SystemUtils::GetCurrentExplorerPath();
                        if (targetPath.empty()) targetPath = g_FallbackTransferPath;
                        
                        uint32_t taskId = g_NextTaskId++;
                        taskId |= 0x80000000;
                        
                        auto task = std::make_shared<FileTransferTask>();
                        task->taskId = taskId;
                        task->isSender = false;
                        task->deviceName = "Master";
                        task->targetPath = targetPath;
                        {
                            std::lock_guard<std::mutex> lock(g_FileClipMutex);
                            task->receivedRoots = g_ReceivedRoots;
                        }
                        {
                            std::lock_guard<std::mutex> lock(g_TaskMutex);
                            g_TransferTasks[taskId] = task;
                        }

                        std::vector<char> reqPkt;
                        reqPkt.push_back(16);
                        unsigned int nTaskId = htonl(taskId);
                        reqPkt.insert(reqPkt.end(), (char*)&nTaskId, (char*)&nTaskId + 4);
                        
                        unsigned int nDevLen = htonl(task->deviceName.length());
                        reqPkt.insert(reqPkt.end(), (char*)&nDevLen, (char*)&nDevLen + 4);
                        reqPkt.insert(reqPkt.end(), task->deviceName.begin(), task->deviceName.end());
                        
                        unsigned int nPathLen = htonl(task->targetPath.length());
                        reqPkt.insert(reqPkt.end(), (char*)&nPathLen, (char*)&nPathLen + 4);
                        reqPkt.insert(reqPkt.end(), targetPath.begin(), targetPath.end());

                        if (g_ClientSock != INVALID_SOCKET_HANDLE) {
                            std::lock_guard<std::mutex> lock(g_SockLock);
                            NetUtils::SendAll(g_ClientSock, reqPkt.data(), reqPkt.size());
                        }

                        if (g_MainObject) {
                            QCoreApplication::postEvent(g_MainObject, new FileTransferPreparingEvent(taskId, QString::fromStdString(task->deviceName), QString::fromStdString(task->targetPath)));
                        }
                    }
                }
                processed += 13;
            }
            else if (flag == 3) { 
                if (remaining < 5) break;
                unsigned int nLen; memcpy(&nLen, ptr + 1, 4);
                int len = ntohl(nLen);
                if (len < 0 || remaining < 5 + len) break;
                std::string received(ptr + 5, len);
                if (g_Context->Clipboard) g_Context->Clipboard->SetLocalClipboard(received);
                processed += (5 + len);
            }
            else if (flag == 4) { 
                if (remaining < 5) break;
                char pongPacket[5] = { 5 };
                memcpy(pongPacket + 1, ptr + 1, 4);
                std::lock_guard<std::mutex> lock(g_SockLock);
                send(g_ClientSock, pongPacket, 5, 0);
                processed += 5;
            }
            else if (flag == 6) { 
                if (remaining < 22) break; 
                unsigned int nActive, nTx, nTy, nPx, nPy;
                memcpy(&nActive, ptr + 1, 4);
                memcpy(&nTx, ptr + 5, 4);
                memcpy(&nTy, ptr + 9, 4);
                memcpy(&nPx, ptr + 13, 4);
                memcpy(&nPy, ptr + 17, 4);
                
                g_MirrorActiveIdx = (int)ntohl(nActive);
                g_MirrorTx = (int)ntohl(nTx);
                g_MirrorTy = (int)ntohl(nTy);
                g_RemoteMouseX = (int)ntohl(nPx);
                g_RemoteMouseY = (int)ntohl(nPy);
                g_Locked = (ptr[21] == 1); 

                processed += 22;
            }
            else if (flag == 10) { 
                if (remaining < 25) break;
                unsigned int mW, mH;
                memcpy(&mW, ptr + 1, 4); memcpy(&mH, ptr + 5, 4);
                g_SlaveW = ntohl(mW); g_SlaveH = ntohl(mH); 
                double lx, ly;
                memcpy(&lx, ptr + 9, 8); memcpy(&ly, ptr + 17, 8);
                g_LogicalX = lx;
                g_LogicalY = ly;
                processed += 25;
            }
            else if (flag == 11) { 
                if (remaining < 21) break;
                unsigned int nmW, nmH, nCount;
                double masterScale;
                memcpy(&nmW, ptr+1, 4); memcpy(&nmH, ptr+5, 4); 
                memcpy(&masterScale, ptr+9, 8);
                memcpy(&nCount, ptr+17, 4);
                int masterW = ntohl(nmW); int masterH = ntohl(nmH); int count = ntohl(nCount);
                int currentOffset = 21;
                std::vector<MirrorCtx> newMirror;
                bool safe = true;
                for(int i=0; i<count; ++i) {
                    if (remaining < currentOffset + 30) { safe = false; break; }
                    unsigned int sw, sh, nLen; double lX, lY; char pausedFlag, tcpFailed;
                    memcpy(&sw, ptr + currentOffset, 4); currentOffset += 4;
                    memcpy(&sh, ptr + currentOffset, 4); currentOffset += 4;
                    memcpy(&lX, ptr + currentOffset, 8); currentOffset += 8;
                    memcpy(&lY, ptr + currentOffset, 8); currentOffset += 8;
                    pausedFlag = ptr[currentOffset]; currentOffset += 1; 
                    tcpFailed = ptr[currentOffset]; currentOffset += 1; 
                    memcpy(&nLen, ptr + currentOffset, 4); currentOffset += 4;
                    int nameLen = ntohl(nLen);
                    
                    if (nameLen < 0 || remaining < currentOffset + nameLen + 4) { safe = false; break; }
                    std::string name(ptr + currentOffset, nameLen);
                    currentOffset += nameLen;
                    
                    if (remaining < currentOffset + 4) { safe = false; break; }
                    unsigned int pLen;
                    memcpy(&pLen, ptr + currentOffset, 4); currentOffset += 4;
                    int propsLen = ntohl(pLen);
                    if (propsLen < 0 || remaining < currentOffset + propsLen + 8) { safe = false; break; }
                    std::string props(ptr + currentOffset, propsLen);
                    currentOffset += propsLen;

                    double sScale;
                    memcpy(&sScale, ptr + currentOffset, 8); currentOffset += 8;

                    MirrorCtx m; m.w = ntohl(sw); m.h = ntohl(sh); m.logicalX = lX; m.logicalY = lY; 
                    m.name = name; m.paused = (pausedFlag == 1); m.tcpFileFailed = (tcpFailed == 1); m.sysProps = props;
                    m.scale = sScale;
                    newMirror.push_back(m);
                }
                if (safe) {
                    std::lock_guard<std::mutex> lock(g_MirrorListLock);
                    g_MirrorMasterW = masterW; g_MirrorMasterH = masterH; 
                    g_MirrorMasterScale = masterScale;
                    g_MirrorList = newMirror;
                    processed += currentOffset;
                } else break; 
            } else if (flag == 12) { 
                if (remaining < 5) break;
                g_HasFileUpdate = true;
                unsigned int numFiles; memcpy(&numFiles, ptr+1, 4);
                int fileCount = ntohl(numFiles);
                MDC_LOG_INFO(LogTag::TRANS, "Receive Root file info Flag 12 roots: %d", fileCount);
                
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
                        MDC_LOG_DEBUG(LogTag::TRANS, "Root index %d received name length: %zu", i, name.length());
                    }
                    if (safe) {
                        g_LastCopiedFiles = infos; 
                        processed += currentOffset;
                    } else break;
                }
            } 
            else if (flag == 8) { 
                processed += 1;
                break; 
            }
            else if (flag == 20) { 
                processed += 1;
                MDC_LOG_INFO(LogTag::NET, "Receive TCP handshake request Flag 20 start listener");

                std::thread([](){
                    if (!SystemUtils::HasNetworkConnectivity()) {
                        std::vector<char> pkt;
                        pkt.push_back(21);
                        unsigned int nCount = 0;
                        pkt.insert(pkt.end(), (char*)&nCount, (char*)&nCount + 4);
                        unsigned short nPort = 0;
                        pkt.insert(pkt.end(), (char*)&nPort, (char*)&nPort + 2);
                        std::lock_guard<std::mutex> lock(g_SockLock);
                        if (g_ClientSock != INVALID_SOCKET_HANDLE) {
                            send(g_ClientSock, pkt.data(), pkt.size(), 0);
                        }
                        MDC_LOG_WARN(LogTag::NET, "No network connectivity reject TCP handshake");
                        return;
                    }

                    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                    SOCKADDR_IN addr = {0};
                    addr.sin_family = AF_INET;
                    addr.sin_addr.s_addr = INADDR_ANY;
                    addr.sin_port = 0; 
                    
                    if (bind(s, (SOCKADDR*)&addr, sizeof(addr)) != 0) {
                        MDC_LOG_ERROR(LogTag::NET, "TCP bind failed");
                        closesocket(s); return;
                    }
                    if (listen(s, 1) != 0) {
                        MDC_LOG_ERROR(LogTag::NET, "TCP listen failed");
                        closesocket(s); return;
                    }
                    
                    int len = sizeof(addr);
                    if (getsockname(s, (SOCKADDR*)&addr, &len) != 0) {
                        MDC_LOG_ERROR(LogTag::NET, "TCP getsockname failed");
                        closesocket(s); return;
                    }
                    int port = ntohs(addr.sin_port);
                    
                    auto ips = SystemUtils::GetLocalIPAddresses();
                    
                    std::vector<char> pkt;
                    pkt.push_back(21);
                    unsigned int nCount = htonl((unsigned int)ips.size());
                    pkt.insert(pkt.end(), (char*)&nCount, (char*)&nCount + 4);
                    unsigned short nPort = htons((unsigned short)port);
                    pkt.insert(pkt.end(), (char*)&nPort, (char*)&nPort + 2);
                    
                    for (const auto& ip : ips) {
                        unsigned int nLen = htonl((unsigned int)ip.length());
                        pkt.insert(pkt.end(), (char*)&nLen, (char*)&nLen + 4);
                        pkt.insert(pkt.end(), ip.begin(), ip.end());
                    }

                    {
                        std::lock_guard<std::mutex> lock(g_SockLock);
                        if (g_ClientSock != INVALID_SOCKET_HANDLE) {
                            send(g_ClientSock, pkt.data(), pkt.size(), 0);
                        }
                    }

                    SOCKET client = accept(s, NULL, NULL);
                    closesocket(s); 
                    
                    if (client != INVALID_SOCKET) {
                        MDC_LOG_INFO(LogTag::NET, "TCP handshake success port %d", port);
                        
                        SocketHandle oldFileSock = g_ClientFileSock;
                        g_ClientFileSock = (SocketHandle)client;
                        
                        if (oldFileSock != INVALID_SOCKET_HANDLE && oldFileSock != g_ClientBtFileSock) {
                            NetUtils::CloseSocket(oldFileSock); 
                        }
                        
                        std::thread(FileNetworkReceiver).detach();
                        
                        std::thread([]() {
                            g_LastFilePingTime = SystemUtils::GetTimeMS();
                            SocketHandle mySock = g_ClientFileSock;
                            while (g_Running && g_ClientFileSock == mySock && mySock != INVALID_SOCKET_HANDLE) {
                                int timeout = (mySock != g_ClientBtFileSock) ? 10000 : 15000;
                                if (SystemUtils::GetTimeMS() - g_LastFilePingTime.load() > timeout) {
                                    MDC_LOG_WARN(LogTag::NET, "File TCP heartbeat timeout");
                                    if (g_ClientFileSock == mySock) {
                                        if (mySock != g_ClientBtFileSock) {
                                            NetUtils::CloseSocket(mySock);
                                            g_ClientFileSock = g_ClientBtFileSock;
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
                    } else {
                        MDC_LOG_ERROR(LogTag::NET, "TCP handshake accept fail");
                    }
                }).detach();
            } else if (flag == 19) { 
                if (remaining < 5) break;
                unsigned int nTaskId; memcpy(&nTaskId, ptr + 1, 4);
                uint32_t taskId = ntohl(nTaskId);
                MDC_LOG_INFO(LogTag::TRANS, "Network receive Flag 19 cancel for Task %u", taskId);
                
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
                MDC_LOG_INFO(LogTag::TRANS, "Network receive Flag 22 pause for Task %u paused: %d", taskId, paused);
                
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
            } else if (flag == 25) { 
                if (remaining < 5) break;
                unsigned int pLen; memcpy(&pLen, ptr + 1, 4);
                int propsLen = ntohl(pLen);
                if (propsLen < 0 || remaining < 5 + propsLen) break;
                g_MasterSysProps = std::string(ptr + 5, propsLen);
                processed += (5 + propsLen);
            } else if (flag == 26) {
                if (remaining < 5) break;
                unsigned int nLat; memcpy(&nLat, ptr + 1, 4);
                g_Latency = ntohl(nLat);
                processed += 5;
            } else if (flag == 32) { 
                MDC_LOG_WARN(LogTag::NET, "Receive Flag 32 net lost fallback to BT");
                if (g_ClientFileSock != INVALID_SOCKET_HANDLE && g_ClientFileSock != g_ClientBtFileSock) {
                    NetUtils::CloseSocket(g_ClientFileSock);
                    g_ClientFileSock = g_ClientBtFileSock;
                    {
                        std::lock_guard<std::mutex> tLock(g_TaskMutex);
                        for (auto& kv : g_TransferTasks) {
                            if (!kv.second->paused.exchange(true)) {
                                if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new TransferPauseEvent(kv.first, true));
                            }
                        }
                    }
                } else {
                    g_ClientFileSock = g_ClientBtFileSock;
                }
                processed += 1;
            } else processed++;
        }
        if (processed < total) {
            memmove(buffer.data(), buffer.data() + processed, total - processed);
            offset = total - processed;
            
            if (offset == buffer.size()) {
                buffer.resize(buffer.size() * 2);
            }
        } else offset = 0;
    }
    
    if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new DisconnectedEvent());
}

void SlaveNetworkMonitorThreadFunc() {
    if (!g_IsBluetoothConn) return; 
    bool lastHasNet = SystemUtils::HasNetworkConnectivity();
    std::string lastNetName = SystemUtils::GetActiveNetworkName();
    
    while (g_Running && g_ClientSock != INVALID_SOCKET_HANDLE) {
        bool currentHasNet = SystemUtils::HasNetworkConnectivity();
        std::string currentNetName = SystemUtils::GetActiveNetworkName();
        
        if (lastHasNet && !currentHasNet) {
            char pkt = 32;
            {
                std::lock_guard<std::mutex> lock(g_SockLock);
                if (g_ClientSock != INVALID_SOCKET_HANDLE) send(g_ClientSock, &pkt, 1, 0);
            }
            MDC_LOG_WARN(LogTag::NET, "Network disconnected send Flag 32 and fallback to BT");
            if (g_ClientFileSock != INVALID_SOCKET_HANDLE && g_ClientFileSock != g_ClientBtFileSock) {
                NetUtils::CloseSocket(g_ClientFileSock);
                g_ClientFileSock = g_ClientBtFileSock;
                {
                    std::lock_guard<std::mutex> tLock(g_TaskMutex);
                    for (auto& kv : g_TransferTasks) {
                        if (!kv.second->paused.exchange(true)) {
                            if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new TransferPauseEvent(kv.first, true));
                        }
                    }
                }
            } else {
                g_ClientFileSock = g_ClientBtFileSock;
            }
        } else if ((!lastHasNet && currentHasNet) || (currentHasNet && currentNetName != lastNetName && !currentNetName.empty() && !lastNetName.empty())) {
            char pkt = 33;
            {
                std::lock_guard<std::mutex> lock(g_SockLock);
                if (g_ClientSock != INVALID_SOCKET_HANDLE) send(g_ClientSock, &pkt, 1, 0);
            }
            MDC_LOG_INFO(LogTag::NET, "Network state changed send Flag 33");
        }
        
        lastHasNet = currentHasNet;
        if (!currentNetName.empty()) lastNetName = currentNetName;
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void StartNetworkReceiverThread() {
    std::thread(NetworkReceiver).detach();
    std::thread(SlaveNetworkMonitorThreadFunc).detach(); 
    if (g_ClientFileSock != INVALID_SOCKET_HANDLE) {
        std::thread(FileNetworkReceiver).detach();
        
        std::thread([]() {
            g_LastFilePingTime = SystemUtils::GetTimeMS();
            SocketHandle mySock = g_ClientFileSock;
            while (g_Running && g_ClientFileSock == mySock && mySock != INVALID_SOCKET_HANDLE) {
                int timeout = (mySock != g_ClientBtFileSock) ? 10000 : 15000;
                if (SystemUtils::GetTimeMS() - g_LastFilePingTime.load() > timeout) {
                    MDC_LOG_WARN(LogTag::NET, "File TCP heartbeat timeout");
                    if (g_ClientFileSock == mySock) {
                        if (mySock != g_ClientBtFileSock) {
                            NetUtils::CloseSocket(mySock);
                            g_ClientFileSock = g_ClientBtFileSock;
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