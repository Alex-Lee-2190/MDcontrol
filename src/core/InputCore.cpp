#include "InputCore.h"
#include "Common.h"
#include "SystemUtils.h"
#include "KvmEvents.h" 
#include "MainWindow.h"
#include <QtCore/QCoreApplication> 
#include <QtCore/QMetaObject>
#include <QtWidgets/QMessageBox>
#include <cmath>

struct RectD {
    double x, y, w, h;
    bool contains(double px, double py) {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

RectD GetLogicalRect(std::shared_ptr<SlaveCtx> ctx) {
    double lw = ctx->width / ctx->scale;
    double lh = ctx->height / ctx->scale;
    return {ctx->logicalX, ctx->logicalY, lw, lh};
}

bool InputCore::ProcessMouse(int x, int y, MouseEventType type, int data) {
    MDC_LOG_TRACE(LogTag::KVM, "ProcessMouse x:%d y:%d type:%d data:%d", x, y, type, data);
    uint32_t now = SystemUtils::GetTimeMS();

    if (g_IsRemote) {
        std::shared_ptr<SlaveCtx> ctx;
        {
            std::lock_guard<std::mutex> lock(g_SlaveListLock);
            int idx = g_ActiveSlaveIdx.load();
            if (idx >= 0 && idx < (int)g_SlaveList.size()) {
                ctx = g_SlaveList[idx];
            }
        }

        if (ctx) {
            int cx = g_LocalW / 2;
            int cy = g_LocalH / 2;
            int dx = x - cx;
            int dy = y - cy;

            SystemUtils::SetCursorPos(cx, cy);

            // 丢弃由 SetCursorPos 引起的系统消息队列残留
            if (now - g_LastSwitchTime < 50) {
                dx = 0;
                dy = 0;
            }

            // 丢弃异常的跳变增量
            if (std::abs(dx) > g_LocalW / 3 || std::abs(dy) > g_LocalH / 3) {
                MDC_LOG_WARN(LogTag::KVM, "Huge jump ignored dx: %d dy: %d", dx, dy);
                dx = 0;
                dy = 0;
            }

            if (dx != 0 || dy != 0) {
                double logicalDx = dx / g_LocalScale;
                double logicalDy = dy / g_LocalScale;
                
                RectD myRect = GetLogicalRect(ctx);
                double absX = myRect.x + g_CurTx.load() / ctx->scale;
                double absY = myRect.y + g_CurTy.load() / ctx->scale;
                
                double newAbsX = absX + logicalDx;
                double newAbsY = absY + logicalDy;
                
                bool outOfBounds = !myRect.contains(newAbsX, newAbsY);
                
                if (outOfBounds && !g_Locked && (now - g_LastSwitchTime > 300)) {
                    MDC_LOG_DEBUG(LogTag::KVM, "Out of bounds myRect x:%.1f y:%.1f w:%.1f h:%.1f newAbsX: %.1f newAbsY: %.1f", 
                        myRect.x, myRect.y, myRect.w, myRect.h, newAbsX, newAbsY);

                    int newDeviceIdx = -2; 
                    
                    RectD masterRect = {0.0, 0.0, g_LocalW / g_LocalScale, g_LocalH / g_LocalScale};
                    if (masterRect.contains(newAbsX, newAbsY)) {
                        newDeviceIdx = -1;
                    } else {
                        std::lock_guard<std::mutex> lock(g_SlaveListLock);
                        for (size_t i = 0; i < g_SlaveList.size(); ++i) {
                            if (!g_SlaveList[i]->connected || g_SlaveList[i]->paused.load()) continue;
                            RectD r = GetLogicalRect(g_SlaveList[i]);
                            if (r.contains(newAbsX, newAbsY)) {
                                newDeviceIdx = (int)i;
                                break;
                            }
                        }
                    }
                    
                    if (newDeviceIdx != -2) {
                        SendEvent(7, 0, 0, g_ActiveSlaveIdx.load()); 
                        
                        g_ActiveSlaveIdx = newDeviceIdx;
                        g_LastSwitchTime = now;
                        
                        if (newDeviceIdx == -1) {
                            g_IsRemote = false;
                            int wx = (int)(newAbsX * g_LocalScale);
                            int wy = (int)(newAbsY * g_LocalScale);
                            if (wx < 0) wx = 0; if (wx >= g_LocalW) wx = g_LocalW - 1;
                            if (wy < 0) wy = 0; if (wy >= g_LocalH) wy = g_LocalH - 1;
                            
                            MDC_LOG_INFO(LogTag::KVM, "Switch to Master wx: %d wy: %d", wx, wy);
                            SystemUtils::SetCursorPos(wx, wy);
                        } else {
                            std::shared_ptr<SlaveCtx> newCtx;
                            {
                                std::lock_guard<std::mutex> lock(g_SlaveListLock);
                                if (newDeviceIdx >= 0 && newDeviceIdx < (int)g_SlaveList.size()) {
                                    newCtx = g_SlaveList[newDeviceIdx];
                                }
                            }
                            if (newCtx) {
                                RectD newRect = GetLogicalRect(newCtx);
                                g_CurTx = (int)std::round((newAbsX - newRect.x) * newCtx->scale);
                                g_CurTy = (int)std::round((newAbsY - newRect.y) * newCtx->scale);
                                
                                MDC_LOG_DEBUG(LogTag::KVM, "Switch to Slave %d newRect x:%.1f y:%.1f BeforeClamp_Tx: %d BeforeClamp_Ty: %d", 
                                    newDeviceIdx, newRect.x, newRect.y, g_CurTx.load(), g_CurTy.load());

                                if (g_CurTx < 0) g_CurTx = 0; if (g_CurTx >= newCtx->width) g_CurTx = newCtx->width - 1;
                                if (g_CurTy < 0) g_CurTy = 0; if (g_CurTy >= newCtx->height) g_CurTy = newCtx->height - 1;
                                
                                MDC_LOG_DEBUG(LogTag::KVM, "AfterClamp_Tx: %d AfterClamp_Ty: %d targetWidth: %d", 
                                    g_CurTx.load(), g_CurTy.load(), newCtx->width);

                                SendEvent(7, 1, 0, newDeviceIdx); 
                            } else {
                                g_IsRemote = false;
                                g_ActiveSlaveIdx = -1;
                            }
                        }
                        
                        UpdateUI();
                        return true;
                    } else {
                        MDC_LOG_DEBUG(LogTag::KVM, "Out of bounds but no adjacent device found clamp to edge");
                    }
                }
                
                if (newAbsX < myRect.x) newAbsX = myRect.x;
                if (newAbsX >= myRect.x + myRect.w) newAbsX = myRect.x + myRect.w - 0.001;
                if (newAbsY < myRect.y) newAbsY = myRect.y;
                if (newAbsY >= myRect.y + myRect.h) newAbsY = myRect.y + myRect.h - 0.001;
                
                g_CurTx = (int)std::round((newAbsX - myRect.x) * ctx->scale);
                g_CurTy = (int)std::round((newAbsY - myRect.y) * ctx->scale);
                if (g_CurTx < 0) g_CurTx = 0; if (g_CurTx >= ctx->width) g_CurTx = ctx->width - 1;
                if (g_CurTy < 0) g_CurTy = 0; if (g_CurTy >= ctx->height) g_CurTy = ctx->height - 1;
                
                g_HasUpdate = true;
            }
            
            if (type == LeftDown) SendEvent(1, 1, 1);
            else if (type == LeftUp) SendEvent(1, 1, 0);
            else if (type == RightDown) SendEvent(1, 2, 1);
            else if (type == RightUp) SendEvent(1, 2, 0);
            else if (type == MiddleDown) SendEvent(1, 3, 1); 
            else if (type == MiddleUp) SendEvent(1, 3, 0);
            else if (type == XButtonDown) SendEvent(1, 3 + data, 1); 
            else if (type == XButtonUp) SendEvent(1, 3 + data, 0);
            else if (type == Wheel) SendEvent(2, 0, data / 120); 
            
            return true; 
        }
    } else {
        if (!g_Locked && (now - g_LastSwitchTime > 300)) {
            double absX = x / g_LocalScale;
            double absY = y / g_LocalScale;
            
            bool atEdge = (x <= 0 || x >= g_LocalW - 1 || y <= 0 || y >= g_LocalH - 1);
            if (atEdge) {
                double pushX = 0, pushY = 0;
                if (x <= 0) pushX = -1.0;
                if (x >= g_LocalW - 1) pushX = 1.0;
                if (y <= 0) pushY = -1.0;
                if (y >= g_LocalH - 1) pushY = 1.0;
                
                double testX = absX + pushX;
                double testY = absY + pushY;
                
                std::lock_guard<std::mutex> lock(g_SlaveListLock);
                for (size_t i = 0; i < g_SlaveList.size(); ++i) {
                    auto& ctx = g_SlaveList[i];
                    if (!ctx->connected || ctx->paused.load()) continue;
                    
                    RectD r = GetLogicalRect(ctx);
                    if (r.contains(testX, testY)) {
                        MDC_LOG_INFO(LogTag::KVM, "Master boundary breached Switch to Slave %d testX: %.1f testY: %.1f targetRect x:%.1f y:%.1f", 
                            i, testX, testY, r.x, r.y);

                        g_IsRemote = true;
                        g_ActiveSlaveIdx = (int)i;
                        g_LastSwitchTime = now;
                        
                        g_CurTx = (int)std::round((testX - r.x) * ctx->scale);
                        g_CurTy = (int)std::round((testY - r.y) * ctx->scale);

                        MDC_LOG_DEBUG(LogTag::KVM, "BeforeClamp_Tx: %d BeforeClamp_Ty: %d", g_CurTx.load(), g_CurTy.load());

                        if (g_CurTx < 0) g_CurTx = 0; if (g_CurTx >= ctx->width) g_CurTx = ctx->width - 1;
                        if (g_CurTy < 0) g_CurTy = 0; if (g_CurTy >= ctx->height) g_CurTy = ctx->height - 1;
                        
                        MDC_LOG_DEBUG(LogTag::KVM, "AfterClamp_Tx: %d AfterClamp_Ty: %d", g_CurTx.load(), g_CurTy.load());

                        SendEvent(7, 1, 0, (int)i);
                        g_HasUpdate = true;
                        UpdateUI();
                        
                        SystemUtils::SetCursorPos(g_LocalW / 2, g_LocalH / 2);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool InputCore::ProcessKey(int vk, int scan, bool down, bool sys) {
    MDC_LOG_TRACE(LogTag::KVM, "ProcessKey vk:%d scan:%d down:%d sys:%d", vk, scan, down, sys);
    if (vk >= 0 && vk < 256) {
        if (vk == 0x11 || vk == 0xA2 || vk == 0xA3) m_ctrlDown = down;
        if (vk == 0x12 || vk == 0xA4 || vk == 0xA5) m_altDown = down;
        if (vk == 0x10 || vk == 0xA0 || vk == 0xA1) m_shiftDown = down;

        bool isRepeat = (down && m_keyStates[vk]);
        m_keyStates[vk] = down;

        if (down && !isRepeat) {
            auto checkMod = [&](int modType) {
                bool needCtrl = (modType == 1 || modType == 4);
                bool needAlt = (modType == 2 || modType == 4);
                bool needShift = (modType == 3);
                return (m_ctrlDown == needCtrl) && (m_altDown == needAlt) && (m_shiftDown == needShift);
            };

            if (g_HkToggleVk && vk == g_HkToggleVk && checkMod(g_HkToggleMod)) {
                QMetaObject::invokeMethod(g_MainObject, [](){
                    ControlWindow* w = (ControlWindow*)g_MainObject;
                    if (w) {
                        if (w->isHidden() || w->isMinimized()) {
                            w->showNormal();
                            w->activateWindow();
                        } else {
                            w->hide();
                        }
                    }
                });
                return true;
            }
            if (g_HkLockVk && vk == g_HkLockVk && checkMod(g_HkLockMod)) {
                QMetaObject::invokeMethod(g_MainObject, [](){
                    if (g_ClientSock != INVALID_SOCKET_HANDLE) {
                        char pkt = 23;
                        std::lock_guard<std::mutex> lock(g_SockLock);
                        send(g_ClientSock, &pkt, 1, 0);
                    } else {
                        g_Locked = !g_Locked;
                        UpdateUI();
                        if (g_MainObject) {
                            QMetaObject::invokeMethod(g_MainObject, "update", Qt::QueuedConnection);
                        }
                    }
                });
                return true;
            }
            if (g_HkDisconnVk && vk == g_HkDisconnVk && checkMod(g_HkDisconnMod)) {
                QMetaObject::invokeMethod(g_MainObject, [](){
                    ControlWindow* w = (ControlWindow*)g_MainObject;
                    if (w) {
                        w->onStop();
                    }
                });
                return true;
            }
            if (g_HkExitVk && vk == g_HkExitVk && checkMod(g_HkExitMod)) {
                exit(0);
                return true;
            }
        }
    }
    
    if (g_IsRemote) {
        if (vk == 0x56 && down) { 
            if (m_ctrlDown) {
                if (g_HasFileUpdate) {
                    int connectedSlaves = 0;
                    {
                        std::lock_guard<std::mutex> lk(g_SlaveListLock);
                        for(auto& s : g_SlaveList) if(s->connected) connectedSlaves++;
                    }
                    if (connectedSlaves > 1) {
                        std::lock_guard<std::mutex> tLock(g_TaskMutex);
                        if (!g_TransferTasks.empty()) {
                            QMetaObject::invokeMethod(g_MainObject, [](){
                                QMessageBox::warning((QWidget*)g_MainObject, T("提示"), T("多设备连接时，同时只能进行一个文件传输任务。"));
                            });
                            return true; 
                        }
                    }
                    MDC_LOG_INFO(LogTag::KVM, "Trigger file paste event Type 8");
                    g_HasFileUpdate = false; 
                    SendEvent(8, 0, 0, g_ActiveSlaveIdx.load()); 
                    return true; 
                }
            }
        }
        SendEvent(down ? 3 : 4, vk, scan, g_ActiveSlaveIdx.load());
        return true;
    } else {
        if (vk == 0x56 && down) { 
            if (m_ctrlDown && g_RemoteFilesAvailable) {
                MDC_LOG_INFO(LogTag::KVM, "Local V pressed trigger remote file download async");
                if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new PrepareFileDownloadEvent());
                return true; 
            }
        }
    }
    return false;
}