#include "Common.h"
#include "SystemUtils.h"
#include "KvmContext.h"
#include <vector>
#include <chrono>

std::queue<InputPkt> g_InputQueue;
std::mutex g_InputQueueLock;

void SenderThread() {
    std::thread([]() {
        char displayBuffer[22]; displayBuffer[0] = 6; 
        uint32_t lastTopoCheck = 0;
        
        int last_active = -99;
        bool last_locked = false;

        while (g_Running) {
            uint32_t now = SystemUtils::GetTimeMS();
            
            int active = g_ActiveSlaveIdx.load();
            int curTx = g_CurTx.load();
            int curTy = g_CurTy.load();
            int px = 0, py = 0;
            SystemUtils::GetCursorPos(px, py);
            bool locked = g_Locked.load();

            // Filter out high-frequency mouse movement noise, only broadcast focus switch and lock events
            bool displayChanged = (active != last_active || locked != last_locked);

            if (displayChanged) { 
                last_active = active;
                last_locked = locked;

                unsigned int nActive = htonl(active);
                unsigned int nTx = htonl(curTx);
                unsigned int nTy = htonl(curTy);
                unsigned int nPx = htonl(px);
                unsigned int nPy = htonl(py);

                memcpy(displayBuffer + 1, &nActive, 4);
                memcpy(displayBuffer + 5, &nTx, 4);
                memcpy(displayBuffer + 9, &nTy, 4);
                memcpy(displayBuffer + 13, &nPx, 4);
                memcpy(displayBuffer + 17, &nPy, 4);
                displayBuffer[21] = locked ? 1 : 0;

                std::vector<std::shared_ptr<SlaveCtx>> snapshot;
                {
                    std::lock_guard<std::mutex> lk(g_SlaveListLock);
                    for(auto& s : g_SlaveList) {
                        if (s->connected && s->sock != INVALID_SOCKET_HANDLE) snapshot.push_back(s);
                    }
                }

                for (auto& s : snapshot) {
                    std::lock_guard<std::mutex> netLock(s->sendLock);
                    send(s->sock, displayBuffer, 22, 0); 
                }
            }

            if (now - lastTopoCheck > 500) {
                lastTopoCheck = now;
                std::vector<std::shared_ptr<SlaveCtx>> snapshot;
                {
                    std::lock_guard<std::mutex> lk(g_SlaveListLock);
                    for(auto& s : g_SlaveList) {
                        if (s->connected && s->sock != INVALID_SOCKET_HANDLE) snapshot.push_back(s);
                    }
                }

                std::vector<char> topoBuf;
                topoBuf.push_back(11);
                unsigned int nmW = htonl(g_LocalW);
                unsigned int nmH = htonl(g_LocalH);
                unsigned int nCount = htonl((unsigned int)snapshot.size());
                
                char tmp[4];
                memcpy(tmp, &nmW, 4); topoBuf.insert(topoBuf.end(), tmp, tmp+4);
                memcpy(tmp, &nmH, 4); topoBuf.insert(topoBuf.end(), tmp, tmp+4);
                
                char dTmp[8];
                double mScale = g_LocalScale;
                memcpy(dTmp, &mScale, 8); topoBuf.insert(topoBuf.end(), dTmp, dTmp+8);
                
                memcpy(tmp, &nCount, 4); topoBuf.insert(topoBuf.end(), tmp, tmp+4);

                for (auto& s : snapshot) {
                    unsigned int sw = htonl(s->width);
                    unsigned int sh = htonl(s->height);
                    double lX = s->logicalX;
                    double lY = s->logicalY;
                    char pausedFlag = s->paused.load() ? 1 : 0; 
                    char tcpFailed = s->tcpFileFailed.load() ? 1 : 0; 
                    unsigned int nNameLen = htonl((unsigned int)s->name.length());
                    unsigned int nPropsLen = htonl((unsigned int)s->sysProps.length()); 
                    double sScale = s->scale;

                    memcpy(tmp, &sw, 4); topoBuf.insert(topoBuf.end(), tmp, tmp+4);
                    memcpy(tmp, &sh, 4); topoBuf.insert(topoBuf.end(), tmp, tmp+4);
                    char coordBytes[16]; memcpy(coordBytes, &lX, 8); memcpy(coordBytes + 8, &lY, 8);
                    topoBuf.insert(topoBuf.end(), coordBytes, coordBytes+16);
                    topoBuf.push_back(pausedFlag); 
                    topoBuf.push_back(tcpFailed); 
                    memcpy(tmp, &nNameLen, 4); topoBuf.insert(topoBuf.end(), tmp, tmp+4);
                    topoBuf.insert(topoBuf.end(), s->name.begin(), s->name.end());
                    memcpy(tmp, &nPropsLen, 4); topoBuf.insert(topoBuf.end(), tmp, tmp+4); 
                    topoBuf.insert(topoBuf.end(), s->sysProps.begin(), s->sysProps.end()); 
                    memcpy(dTmp, &sScale, 8); topoBuf.insert(topoBuf.end(), dTmp, dTmp+8);
                }

                std::string currentTopoStr(topoBuf.begin(), topoBuf.end());
                for (auto& s : snapshot) {
                    if (s->lastSentTopo != currentTopoStr) {
                        std::lock_guard<std::mutex> netLock(s->sendLock);
                        send(s->sock, topoBuf.data(), (int)topoBuf.size(), 0);
                        s->lastSentTopo = currentTopoStr;
                    }
                }
            }

            static uint32_t lastReconCheck = 0;
            if (now - lastReconCheck > 5000) {
                lastReconCheck = now;
                std::vector<std::shared_ptr<SlaveCtx>> failedCtxs;
                {
                    std::lock_guard<std::mutex> lk(g_SlaveListLock);
                    for(auto& s : g_SlaveList) {
                        if (s->connected && s->tcpFileFailed.load() && !s->isBluetooth) failedCtxs.push_back(s);
                    }
                }
                if (!failedCtxs.empty() && SystemUtils::HasNetworkConnectivity()) {
                    for (auto& s : failedCtxs) {
                        char pkt = 20;
                        std::lock_guard<std::mutex> netLock(s->sendLock);
                        if (send(s->sock, &pkt, 1, 0) == SOCKET_ERROR_CODE) {
                            s->connected = false;
                        }
                    }
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }).detach();

    SystemUtils::BeginHighPrecisionTimer();
    char posBuffer[9] = { 1 };
    
    while (g_Running) {
        bool sendPos = false;
        int tx = 0, ty = 0;
        
        if (g_IsRemote && g_HasUpdate) {
             tx = g_CurTx.load();
             ty = g_CurTy.load();
             sendPos = true;
             g_HasUpdate = false; 
        }

        std::vector<InputPkt> eventsToSend;
        {
            std::lock_guard<std::mutex> qLock(g_InputQueueLock);
            while (!g_InputQueue.empty()) {
                eventsToSend.push_back(g_InputQueue.front());
                g_InputQueue.pop();
            }
        }

        if (sendPos || !eventsToSend.empty()) {
            std::shared_ptr<SlaveCtx> ctx;
            {
                std::lock_guard<std::mutex> lk(g_SlaveListLock);
                int idx = g_ActiveSlaveIdx.load();
                if (idx >= 0 && idx < (int)g_SlaveList.size()) ctx = g_SlaveList[idx];
            }

            if (ctx && ctx->sock != INVALID_SOCKET_HANDLE) {
                std::vector<char> batch;
                if (sendPos) {
                     unsigned int nx = htonl(tx), ny = htonl(ty);
                     memcpy(posBuffer + 1, &nx, 4); memcpy(posBuffer + 5, &ny, 4);
                     batch.insert(batch.end(), posBuffer, posBuffer + 9);
                }
                for (const auto& pkt : eventsToSend) {
                    batch.insert(batch.end(), pkt.data, pkt.data + pkt.len);
                }
                
                std::lock_guard<std::mutex> netLock(ctx->sendLock);
                if (!NetUtils::SendAll(ctx->sock, batch.data(), batch.size())) {
                    ctx->connected = false;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    SystemUtils::EndHighPrecisionTimer();
}

void SendEvent(int type, int p1, int p2, int targetIdx) {
    InputPkt pkt;
    pkt.data[0] = 2; 
    unsigned int nT = htonl(type), nP1 = htonl(p1), nP2 = htonl(p2);
    memcpy(pkt.data + 1, &nT, 4); 
    memcpy(pkt.data + 5, &nP1, 4); 
    memcpy(pkt.data + 9, &nP2, 4);
    pkt.len = 13;
    pkt.targetIdx = targetIdx;
    std::lock_guard<std::mutex> lock(g_InputQueueLock);
    g_InputQueue.push(pkt);
}