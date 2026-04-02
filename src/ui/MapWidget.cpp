#include "MainWindow.h"
#include "Common.h"
#include "SystemUtils.h"
#include "IconDrawer.h"
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtCore/QTimer>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtGui/QKeyEvent>
#include <QtWidgets/QMenu>
#include <QtGui/QAction>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QToolTip>
#include <QtGui/QGuiApplication>
#include <QtGui/QStyleHints>
#include <cmath>

MapWidget::MapWidget(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(300, 200); 
    setMouseTracking(true); 
    
    QTimer* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, QOverload<>::of(&MapWidget::update));
    timer->start(16); 
}

void MapWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing); 
    
    bool isDark = (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    QColor backgroundColor = isDark ? QColor(40, 44, 52) : QColor(225, 228, 232);
    QColor textColor = QColor(255, 255, 255, 240); // 始终保持白色字体
    p.fillRect(rect(), backgroundColor);  

    if (g_LocalW == 0) return; 

    int padding = 40; 
    int cw = width(), ch = height();
    
    int masterW, masterH;
    std::vector<MirrorCtx> slavesToDraw;
    int activeDrawIdx = -1; 
    
    bool isSlaveMode = (g_ClientSock != INVALID_SOCKET_HANDLE);
    
    if (isSlaveMode) {
        std::lock_guard<std::mutex> lock(g_MirrorListLock);
        masterW = g_MirrorMasterW;
        masterH = g_MirrorMasterH;
        slavesToDraw = g_MirrorList;
        activeDrawIdx = g_MirrorActiveIdx; 
    } else {
        masterW = g_LocalW;
        masterH = g_LocalH;
        std::lock_guard<std::mutex> lock(g_SlaveListLock);
        int realActiveIdx = g_ActiveSlaveIdx.load();
        int drawIdx = 0;
        for(size_t i = 0; i < g_SlaveList.size(); ++i) {
            const auto& s = g_SlaveList[i];
            
            if ((int)i == realActiveIdx) activeDrawIdx = drawIdx;
            MirrorCtx m;
            m.w = s->width;
            m.h = s->height;
            m.logicalX = s->logicalX;
            m.logicalY = s->logicalY;
            m.name = s->name;
            m.paused = s->paused.load();
            m.tcpFileFailed = s->tcpFileFailed.load();
            m.sysProps = s->sysProps;
            m.scale = s->scale;
            slavesToDraw.push_back(m);
            drawIdx++;
        }
    }

    double logicalMW = masterW / (isSlaveMode ? g_MirrorMasterScale : g_LocalScale);
    double logicalMH = masterH / (isSlaveMode ? g_MirrorMasterScale : g_LocalScale);
    
    double totalW = logicalMW * 3; 
    double totalH = logicalMH * 3;
    double s = std::min((cw - 2 * padding) / totalW, (ch - 2 * padding) / totalH) * m_zoomFactor;

    double smW = logicalMW * s;
    double smH = logicalMH * s;
    double mxo = (cw - smW) / 2.0 + m_viewOffset.x();
    double myo = (ch - smH) / 2.0 + m_viewOffset.y();
    QRectF masterRect(mxo, myo, smW, smH);
    
    m_currentLocalRect = masterRect;
    m_currentSlaveRects.clear();
    std::vector<QRectF> tempSlaveRects;

    for (size_t i = 0; i < slavesToDraw.size(); ++i) {
        auto& ctx = slavesToDraw[i];
        double logicalSW = ctx.w / ctx.scale;
        double logicalSH = ctx.h / ctx.scale;
        double ssW = logicalSW * s;
        double ssH = logicalSH * s;
        double sxo, syo;

        if (!isSlaveMode && m_isDragging && (int)i == m_dragSlaveIdx) {
            QPoint currentMouse = this->mapFromGlobal(QCursor::pos());
            QPoint delta = currentMouse - m_dragStartPos;
            sxo = m_dragStartRemotePos.x() + delta.x();
            syo = m_dragStartRemotePos.y() + delta.y();
        } else {
            sxo = mxo + ctx.logicalX * s;
            syo = myo + ctx.logicalY * s;
        }
        QRectF slaveRect(sxo, syo, ssW, ssH);
        tempSlaveRects.push_back(slaveRect);
        m_currentSlaveRects.push_back(slaveRect); 
    }

    // ===== Layer 1: Backgrounds and texts =====
    
    QFont oldFont = p.font();
    QFont nameFont = oldFont;
    int scaledPt = std::max(6, (int)(12 * m_zoomFactor));
    nameFont.setPointSize(scaledPt);
    nameFont.setBold(true);
    p.setFont(nameFont);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 60)); 
    p.drawRoundedRect(masterRect.translated(3, 3), 2, 2);
    QColor masterColor = isDark ? QColor(144, 147, 153) : QColor(200, 200, 200); 
    if (!isSlaveMode && g_ActiveSlaveIdx == -1) masterColor = QColor(64, 158, 255); 
    else if (isSlaveMode && g_MirrorActiveIdx == -1) masterColor = QColor(103, 194, 58); 
    p.setBrush(masterColor);
    p.drawRoundedRect(masterRect, 2, 2);
    
    if (m_selectedDeviceIdx == -2) {
        p.setPen(QPen(QColor(255, 204, 0), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(masterRect, 2, 2);
    }

    QString masterNameStr = "Master";
    if (isSlaveMode) {
        QString props = QString::fromStdString(g_MasterSysProps);
        int idx = props.indexOf("设备名称: ");
        if (idx != -1) {
            int end = props.indexOf('\n', idx);
            masterNameStr = props.mid(idx + 6, end - idx - 6).trimmed();
        } else {
            idx = props.indexOf("Device Name: ");
            if (idx != -1) {
                int end = props.indexOf('\n', idx);
                masterNameStr = props.mid(idx + 13, end - idx - 13).trimmed();
            }
        }
    } else {
        masterNameStr = QString::fromStdString(g_MyName);
    }

    p.setPen(textColor);
    p.drawText(masterRect, Qt::AlignCenter, T("%1 (主机)").arg(masterNameStr));

    for (size_t i = 0; i < slavesToDraw.size(); ++i) {
        QRectF slaveRect = tempSlaveRects[i];
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 60)); 
        p.drawRoundedRect(slaveRect.translated(3, 3), 2, 2);
        QColor bg = isDark ? QColor(144, 147, 153) : QColor(200, 200, 200); 
        if (!isSlaveMode) {
            if (m_isDragging && m_dragSlaveIdx == (int)i) bg = QColor(230, 162, 60);
            else if (activeDrawIdx == (int)i) bg = QColor(103, 194, 58); 
        } else {
            if (g_SlaveFocused && g_MirrorActiveIdx == (int)i) bg = QColor(64, 158, 255);
            else if (g_MirrorActiveIdx == (int)i) bg = QColor(103, 194, 58);
        }
        p.setBrush(bg);
        p.drawRoundedRect(slaveRect, 2, 2);
        
        if (m_selectedDeviceIdx == (int)i) {
            p.setPen(QPen(QColor(255, 204, 0), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(slaveRect, 2, 2);
        }

        p.setPen(textColor);
        p.drawText(slaveRect, Qt::AlignCenter, QString::fromStdString(slavesToDraw[i].name));
        
        bool isConnected = true;
        if (!isSlaveMode) {
             std::lock_guard<std::mutex> lock(g_SlaveListLock);
             if (i < g_SlaveList.size()) isConnected = g_SlaveList[i]->connected;
        }

        if (!isConnected) {
             p.setBrush(QColor(0, 0, 0, 150));
             p.setPen(Qt::NoPen);
             p.drawRoundedRect(slaveRect, 2, 2);

             double sz = qMin(slaveRect.width(), slaveRect.height()) * 0.4;
             if (sz < 30) sz = 30;
             if (sz > 60) sz = 60;
             QRectF iconRect(slaveRect.center().x() - sz / 2.0, slaveRect.center().y() - sz / 2.0, sz, sz);
             IconDrawer::drawDisconnectedIcon(p, iconRect);
        } else {
             if (slavesToDraw[i].paused) {
                 p.setBrush(QColor(0, 0, 0, 150));
                 p.setPen(Qt::NoPen);
                 p.drawRoundedRect(slaveRect, 2, 2);
                 double sz = qMin(slaveRect.width(), slaveRect.height()) * 0.4;
                 if (sz > 60) sz = 60;
                 QRectF iconRect(slaveRect.center().x() - sz / 2.0, slaveRect.center().y() - sz / 2.0, sz, sz);
                 IconDrawer::drawPausePlayIcon(p, iconRect, false);
             }
             if (slavesToDraw[i].tcpFileFailed) {
                 double sz = 24.0;
                 QRectF wifiRect(slaveRect.right() - sz - 4, slaveRect.bottom() - sz - 4, sz, sz);
                 IconDrawer::drawWifiFailedIcon(p, wifiRect);
             }
        }
    }

    p.setFont(oldFont);

    // ===== Layer 2: Simulated cursor (red dot) =====
    double dotX = 0, dotY = 0;
    bool drawDot = false;

    if (!isSlaveMode) {
        if (g_IsRemote && activeDrawIdx != -1 && activeDrawIdx < (int)tempSlaveRects.size()) {
            QRectF r = tempSlaveRects[activeDrawIdx];
            auto& ctx = slavesToDraw[activeDrawIdx];
            int tx = g_CurTx.load();
            int ty = g_CurTy.load();
            int cw_safe = ctx.w > 0 ? ctx.w : 1;
            int ch_safe = ctx.h > 0 ? ctx.h : 1;
            if (tx < 0) tx = 0; if (tx > cw_safe) tx = cw_safe;
            if (ty < 0) ty = 0; if (ty > ch_safe) ty = ch_safe;
            dotX = r.x() + (tx / (double)cw_safe) * r.width();
            dotY = r.y() + (ty / (double)ch_safe) * r.height();
            drawDot = true;
        } else {
            int px, py;
            SystemUtils::GetCursorPos(px, py);
            dotX = mxo + (px / g_LocalScale) * s;
            dotY = myo + (py / g_LocalScale) * s;
            drawDot = true;
        }
    } else {
        if (g_SlaveFocused && g_MirrorActiveIdx >= 0 && g_MirrorActiveIdx < (int)tempSlaveRects.size()) {
            int px, py;
            SystemUtils::GetCursorPos(px, py);
            QRectF r = tempSlaveRects[g_MirrorActiveIdx];
            dotX = r.x() + (px / (double)g_LocalW) * r.width();
            dotY = r.y() + (py / (double)g_LocalH) * r.height();
            drawDot = true;
        } else if (g_MirrorActiveIdx == -1) {
            int rmx = g_RemoteMouseX.load();
            int rmy = g_RemoteMouseY.load();
            if (rmx < 0) rmx = 0;
            if (rmy < 0) rmy = 0;
            dotX = mxo + (rmx / g_MirrorMasterScale) * s;
            dotY = myo + (rmy / g_MirrorMasterScale) * s;
            drawDot = true;
        } else if (g_MirrorActiveIdx >= 0 && g_MirrorActiveIdx < (int)tempSlaveRects.size()) {
            QRectF r = tempSlaveRects[g_MirrorActiveIdx];
            auto& ctx = slavesToDraw[g_MirrorActiveIdx];
            int mx = g_MirrorTx.load();
            int my = g_MirrorTy.load();
            int cw_safe = ctx.w > 0 ? ctx.w : 1;
            int ch_safe = ctx.h > 0 ? ctx.h : 1;
            if (mx < 0) mx = 0; if (mx > cw_safe) mx = cw_safe;
            if (my < 0) my = 0; if (my > ch_safe) my = ch_safe;
            dotX = r.x() + (mx / (double)cw_safe) * r.width();
            dotY = r.y() + (my / (double)ch_safe) * r.height();
            drawDot = true;
        }
    }

    if (drawDot) {
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(QColor(245, 108, 108)); 
        p.drawEllipse(QPointF(dotX, dotY), 4, 4);
    }

    // ===== Layer 3: Translucent mask and lock icon =====
    if (g_Locked) {
        QRectF lockedDeviceRect;
        bool shouldDrawLock = false;

        if (!isSlaveMode) {
            if (g_ActiveSlaveIdx == -1) { lockedDeviceRect = masterRect; shouldDrawLock = true; }
            else if (activeDrawIdx >= 0 && activeDrawIdx < (int)tempSlaveRects.size()) { 
                lockedDeviceRect = tempSlaveRects[activeDrawIdx]; 
                shouldDrawLock = true; 
            }
        } else {
            if (g_MirrorActiveIdx == -1) { lockedDeviceRect = masterRect; shouldDrawLock = true; }
            else if (g_MirrorActiveIdx >= 0 && g_MirrorActiveIdx < (int)tempSlaveRects.size()) { 
                lockedDeviceRect = tempSlaveRects[g_MirrorActiveIdx]; 
                shouldDrawLock = true; 
            }
        }

        if (shouldDrawLock) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 150)); 
            p.drawRoundedRect(lockedDeviceRect, 2, 2);
            double sz = qMin(lockedDeviceRect.width(), lockedDeviceRect.height()) * 0.4;
            if (sz > 60) sz = 60;
            QRectF iconRect(lockedDeviceRect.center().x() - sz / 2.0, lockedDeviceRect.center().y() - sz / 2.0, sz, sz);
            IconDrawer::drawLockIcon(p, iconRect, true);
        }
    }

    // ===== Layer 4: Top floating bar and buttons =====
    double barW = 186, barH = 32; 
    m_barRect = QRectF((cw - barW) / 2.0, 10, barW, barH);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 150));
    p.drawRoundedRect(m_barRect, 8, 8);

    m_lockBtnRect     = QRectF(m_barRect.left() + 8, m_barRect.top() + 3, 26, 26);
    m_pauseBtnRect    = QRectF(m_barRect.left() + 44, m_barRect.top() + 3, 26, 26);
    m_reconBtnRect    = QRectF(m_barRect.left() + 80, m_barRect.top() + 3, 26, 26);
    m_wifiBtnRect     = QRectF(m_barRect.left() + 116, m_barRect.top() + 3, 26, 26);
    m_stopBtnRect     = QRectF(m_barRect.left() + 152, m_barRect.top() + 3, 26, 26);

    bool isLocal = false;
    bool isConnected = false;
    bool isPaused = false;
    bool isTcpFailed = false;

    if (m_selectedDeviceIdx == -2) {
        if (!isSlaveMode) isLocal = true;
    } else if (m_selectedDeviceIdx >= 0) {
        if (isSlaveMode) {
            std::lock_guard<std::mutex> lock(g_MirrorListLock);
            if (m_selectedDeviceIdx < g_MirrorList.size()) {
                if (g_MirrorList[m_selectedDeviceIdx].name == g_MyName) isLocal = true;
                isTcpFailed = g_MirrorList[m_selectedDeviceIdx].tcpFileFailed;
            }
        } else {
            std::lock_guard<std::mutex> lock(g_SlaveListLock);
            if (m_selectedDeviceIdx < g_SlaveList.size()) {
                isConnected = g_SlaveList[m_selectedDeviceIdx]->connected;
                isPaused = g_SlaveList[m_selectedDeviceIdx]->paused.load();
                isTcpFailed = g_SlaveList[m_selectedDeviceIdx]->tcpFileFailed.load();
            }
        }
    }

    bool canLock = isLocal;
    bool canPause = (!isLocal && !isSlaveMode && m_selectedDeviceIdx >= 0 && isConnected);
    bool canRecon = (!isLocal && !isSlaveMode && m_selectedDeviceIdx >= 0 && !isConnected);
    bool canWifi = (!isLocal && !isSlaveMode && m_selectedDeviceIdx >= 0 && isConnected && isTcpFailed); 
    bool canStop = isLocal || (!isLocal && !isSlaveMode && m_selectedDeviceIdx >= 0 && isConnected); 

    p.setOpacity(canLock ? 1.0 : 0.3);
    if (m_pressedBtn == 1) {
        p.setBrush(QColor(255, 255, 255, 40));
        p.drawRoundedRect(m_lockBtnRect, 4, 4);
    }
    IconDrawer::drawLockIcon(p, m_pressedBtn == 1 ? m_lockBtnRect.translated(1, 1) : m_lockBtnRect, g_Locked);
    
    p.setOpacity(canPause ? 1.0 : 0.3);
    if (m_pressedBtn == 2) {
        p.setBrush(QColor(255, 255, 255, 40));
        p.drawRoundedRect(m_pauseBtnRect, 4, 4);
    }
    IconDrawer::drawPausePlayIcon(p, m_pressedBtn == 2 ? m_pauseBtnRect.translated(1, 1) : m_pauseBtnRect, isPaused);

    p.setOpacity(canRecon ? 1.0 : 0.3);
    if (m_pressedBtn == 3) {
        p.setBrush(QColor(255, 255, 255, 40));
        p.drawRoundedRect(m_reconBtnRect, 4, 4);
    }
    IconDrawer::drawReconnectIcon(p, m_pressedBtn == 3 ? m_reconBtnRect.translated(1, 1) : m_reconBtnRect);

    p.setOpacity(canWifi ? 1.0 : 0.3);
    if (m_pressedBtn == 4) {
        p.setBrush(QColor(255, 255, 255, 40));
        p.drawRoundedRect(m_wifiBtnRect, 4, 4);
    }
    IconDrawer::drawWifiIcon(p, m_pressedBtn == 4 ? m_wifiBtnRect.translated(1, 1) : m_wifiBtnRect, isTcpFailed, Qt::white);

    p.setOpacity(canStop ? 1.0 : 0.3);
    if (m_pressedBtn == 6) {
        p.setBrush(QColor(255, 255, 255, 40));
        p.drawRoundedRect(m_stopBtnRect, 4, 4);
    }
    IconDrawer::drawStopIcon(p, m_pressedBtn == 6 ? m_stopBtnRect.translated(1, 1) : m_stopBtnRect);

    p.setOpacity(1.0);

    // ===== Layer 5: Latency widget =====
    m_latencyRect = QRectF(m_barRect.right() + 10, m_barRect.top(), 56, m_barRect.height() - 1);
    
    uint32_t currentLat = 0;
    bool showLatencyColor = false;
    
    if (isSlaveMode) {
        currentLat = g_Latency.load();
        showLatencyColor = true;
    } else {
        int idx = g_ActiveSlaveIdx.load();
        std::lock_guard<std::mutex> lock(g_SlaveListLock);
        if (idx >= 0 && idx < (int)g_SlaveList.size()) {
            currentLat = g_SlaveList[idx]->latency.load();
            showLatencyColor = true;
        } else if (!g_SlaveList.empty()) {
            currentLat = g_SlaveList[0]->latency.load();
            showLatencyColor = true;
        }
    }
    
    QColor latColor = isDark ? QColor(144, 147, 153) : QColor(170, 170, 170); 
    if (showLatencyColor) {
        if (currentLat < 20) latColor = QColor(103, 194, 58); 
        else if (currentLat < 50) latColor = QColor(230, 162, 60); 
        else latColor = QColor(245, 108, 108); 
    }

    p.setPen(Qt::NoPen);
    p.setBrush(latColor);
    p.drawRoundedRect(m_latencyRect, 8, 8);

    p.setPen(Qt::white);
    QFont f2 = p.font();
    f2.setBold(true);
    p.setFont(f2);
    p.drawText(m_latencyRect, Qt::AlignCenter, QString("%1 ms").arg(currentLat));

    // ===== Layer 6: Corner buttons =====
    double focusBtnSize = 40;
    double zoomBtnSize = 30;

    m_focusBtnRect = QRectF(cw - focusBtnSize - 20, ch - focusBtnSize - 20, focusBtnSize, focusBtnSize);
    m_zoomInBtnRect = QRectF(m_focusBtnRect.left() - zoomBtnSize - 10, m_focusBtnRect.center().y() - zoomBtnSize/2, zoomBtnSize, zoomBtnSize);
    m_zoomOutBtnRect = QRectF(m_zoomInBtnRect.left() - zoomBtnSize - 5, m_zoomInBtnRect.top(), zoomBtnSize, zoomBtnSize);
    
    double leftBtnSize = 40;
    m_addBtnRect = QRectF(20, ch - leftBtnSize - 20, leftBtnSize, leftBtnSize);
    m_settingsBtnRect = QRectF(20 + leftBtnSize + 10, ch - leftBtnSize - 20, leftBtnSize, leftBtnSize);

    auto drawBtn = [&](const QRectF& r, int hoverId, int pressId, int btnType) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 100));
        if (m_hoveredBtn == hoverId) p.setBrush(QColor(0, 0, 0, 150));
        if (m_pressedBtn == pressId) p.setBrush(QColor(0, 0, 0, 200));
        p.drawRoundedRect(r, r.width()/2, r.height()/2);
        
        if (btnType == 7) IconDrawer::drawAddIcon(p, r.adjusted(8, 8, -8, -8));
        else if (btnType == 5) IconDrawer::drawSettingsIcon(p, r.adjusted(8, 8, -8, -8));
        else if (btnType == 9) IconDrawer::drawAddIcon(p, r.adjusted(6, 6, -6, -6));
        else if (btnType == 10) IconDrawer::drawMinusIcon(p, r.adjusted(6, 6, -6, -6));
        else if (btnType == 8) IconDrawer::drawFocusIcon(p, r.adjusted(8, 8, -8, -8));
    };

    drawBtn(m_zoomOutBtnRect, 10, 10, 10);
    drawBtn(m_zoomInBtnRect, 9, 9, 9);
    drawBtn(m_focusBtnRect, 8, 8, 8);
    drawBtn(m_addBtnRect, 7, 7, 7);
    drawBtn(m_settingsBtnRect, 5, 5, 5);
}

void MapWidget::mousePressEvent(QMouseEvent* event) {
    bool isSlaveMode = (g_ClientSock != INVALID_SOCKET_HANDLE);
    
    bool isLocal = false;
    bool isConnected = false;
    bool isPaused = false;
    bool isTcpFailed = false;
    std::shared_ptr<SlaveCtx> targetCtx;

    if (m_selectedDeviceIdx == -2) {
        if (!isSlaveMode) isLocal = true;
    } else if (m_selectedDeviceIdx >= 0) {
        if (isSlaveMode) {
            std::lock_guard<std::mutex> lock(g_MirrorListLock);
            if (m_selectedDeviceIdx < g_MirrorList.size()) {
                if (g_MirrorList[m_selectedDeviceIdx].name == g_MyName) {
                    isLocal = true;
                }
                isTcpFailed = g_MirrorList[m_selectedDeviceIdx].tcpFileFailed;
            }
        } else {
            std::lock_guard<std::mutex> lock(g_SlaveListLock);
            if (m_selectedDeviceIdx < g_SlaveList.size()) {
                targetCtx = g_SlaveList[m_selectedDeviceIdx];
                isConnected = targetCtx->connected;
                isPaused = targetCtx->paused.load();
                isTcpFailed = targetCtx->tcpFileFailed.load();
            }
        }
    }

    if (event->button() == Qt::LeftButton) {
        if (m_barRect.contains(event->pos())) {
            if (m_lockBtnRect.contains(event->pos()) && isLocal) {
                m_pressedBtn = 1;
                QTimer::singleShot(100, this,[this](){ m_pressedBtn = 0; update(); });
                update();
                if (isSlaveMode) {
                    char pkt = 23;
                    std::lock_guard<std::mutex> lock(g_SockLock);
                    send(g_ClientSock, &pkt, 1, 0);
                } else {
                    g_Locked = !g_Locked;
                    UpdateUI();
                    update();
                }
            } else if (m_pauseBtnRect.contains(event->pos()) && !isLocal && !isSlaveMode && m_selectedDeviceIdx >= 0 && isConnected) {
                m_pressedBtn = 2;
                QTimer::singleShot(100, this,[this](){ m_pressedBtn = 0; update(); });
                update();
                if (targetCtx) {
                    targetCtx->paused = !isPaused;
                    update();
                }
            } else if (m_reconBtnRect.contains(event->pos()) && !isLocal && !isSlaveMode && m_selectedDeviceIdx >= 0 && !isConnected) {
                m_pressedBtn = 3;
                QTimer::singleShot(100, this, [this](){ m_pressedBtn = 0; update(); });
                update();
                if (g_MainObject) ((ControlWindow*)g_MainObject)->reconnectSlave(m_selectedDeviceIdx);
            } else if (m_wifiBtnRect.contains(event->pos()) && !isLocal && !isSlaveMode && m_selectedDeviceIdx >= 0 && isConnected && isTcpFailed) {
                m_pressedBtn = 4;
                QTimer::singleShot(100, this, [this](){ m_pressedBtn = 0; update(); });
                update();
                if (targetCtx && targetCtx->sock != INVALID_SOCKET_HANDLE) {
                    char pkt = 20;
                    std::lock_guard<std::mutex> netLock(targetCtx->sendLock);
                    send(targetCtx->sock, &pkt, 1, 0);
                }
            } else if (m_stopBtnRect.contains(event->pos())) {
                if (isLocal) {
                    m_pressedBtn = 6;
                    QTimer::singleShot(100, this, [this](){ m_pressedBtn = 0; update(); });
                    update();
                    if (g_MainObject) ((ControlWindow*)g_MainObject)->onStop();
                } else if (!isSlaveMode && m_selectedDeviceIdx >= 0 && isConnected && targetCtx) {
                    m_pressedBtn = 6;
                    QTimer::singleShot(100, this, [this](){ m_pressedBtn = 0; update(); });
                    update();
                    if (targetCtx->sock != INVALID_SOCKET_HANDLE) {
                        MDC_LOG_INFO(LogTag::UI, "Sending Disconnect (Flag 8) to Slave %d", m_selectedDeviceIdx);
                        char pkt = 8;
                        {
                            std::lock_guard<std::mutex> netLock(targetCtx->sendLock);
                            send(targetCtx->sock, &pkt, 1, 0);
                        }
                        targetCtx->connected = false;
                        NetUtils::ShutdownSocket(targetCtx->sock);
                    }
                }
            }
            return;
        } else if (m_addBtnRect.contains(event->pos())) {
            m_pressedBtn = 7;
            QTimer::singleShot(100, this,[this](){ m_pressedBtn = 0; update(); });
            update();
            if (g_MainObject) ((ControlWindow*)g_MainObject)->showConnectWindow();
            return;
        } else if (m_settingsBtnRect.contains(event->pos())) {
            m_pressedBtn = 5;
            QTimer::singleShot(100, this,[this](){ m_pressedBtn = 0; update(); });
            update();
            if (g_MainObject) ((ControlWindow*)g_MainObject)->openSettings();
            return;
        } else if (m_focusBtnRect.contains(event->pos())) {
            m_pressedBtn = 8;
            QTimer::singleShot(100, this, [this](){ m_pressedBtn = 0; update(); });
            m_viewOffset = {0.0, 0.0};
            update();
            return;
        } else if (m_zoomInBtnRect.contains(event->pos())) {
            m_pressedBtn = 9;
            QTimer::singleShot(100, this, [this](){ m_pressedBtn = 0; update(); });
            m_zoomFactor *= 1.2;
            if (m_zoomFactor > 5.0) m_zoomFactor = 5.0;
            update();
            return;
        } else if (m_zoomOutBtnRect.contains(event->pos())) {
            m_pressedBtn = 10;
            QTimer::singleShot(100, this, [this](){ m_pressedBtn = 0; update(); });
            m_zoomFactor /= 1.2;
            if (m_zoomFactor < 0.2) m_zoomFactor = 0.2;
            update();
            return;
        }
    }

    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
        bool hit = false;
        if (m_currentLocalRect.contains(event->pos())) {
            m_selectedDeviceIdx = -2;
            hit = true;
        } else {
            for (size_t i = 0; i < m_currentSlaveRects.size(); ++i) {
                if (m_currentSlaveRects[i].contains(event->pos())) {
                    m_selectedDeviceIdx = (int)i;
                    hit = true;
                    break;
                }
            }
        }
        if (!hit) {
            m_selectedDeviceIdx = -1;
        }
        update();
    }

    if (event->button() == Qt::LeftButton) {
        if (!isSlaveMode && m_selectedDeviceIdx >= 0) {
            m_isDragging = true;
            m_dragSlaveIdx = m_selectedDeviceIdx;
            m_dragStartPos = event->pos();
            m_dragStartRemotePos = m_currentSlaveRects[m_selectedDeviceIdx].topLeft();
        } else {
            m_isPanning = true;
            m_panStartPos = event->pos();
            m_panStartOffset = m_viewOffset;
        }
    }

    if (event->button() == Qt::RightButton) {
        bool hit = false;
        int hitIndex = -1;
        bool isLocalRight = false;
        bool isMasterRect = false;
        
        if (m_currentLocalRect.contains(event->pos())) {
            hit = true;
            isMasterRect = true;
            if (!isSlaveMode) isLocalRight = true; 
        } else {
            for (size_t i = 0; i < m_currentSlaveRects.size(); ++i) {
                if (m_currentSlaveRects[i].contains(event->pos())) {
                    hit = true; hitIndex = (int)i;
                    std::lock_guard<std::mutex> lock(g_MirrorListLock);
                    if (isSlaveMode && i < g_MirrorList.size() && g_MirrorList[i].name == g_MyName) {
                        isLocalRight = true; 
                    }
                    break;
                }
            }
        }

        if (hit) {
            QMenu menu(this);
            
            if (isLocalRight) {
                QAction* lockAction = menu.addAction(g_Locked ? T("解锁设备") : T("锁定设备"));
                connect(lockAction, &QAction::triggered, [=]() {
                    if (isSlaveMode) {
                        char pkt = 23;
                        std::lock_guard<std::mutex> lock(g_SockLock);
                        send(g_ClientSock, &pkt, 1, 0);
                    } else {
                        g_Locked = !g_Locked;
                        UpdateUI();
                        update();
                    }
                });
            }
            
            if (!isLocalRight) {
                if (!isSlaveMode && hitIndex >= 0) {
                    bool isConnectedRight = false;
                    bool isPausedRight = false;
                    std::shared_ptr<SlaveCtx> targetCtxRight;
                    {
                        std::lock_guard<std::mutex> lock(g_SlaveListLock);
                        if (hitIndex < (int)g_SlaveList.size()) {
                            targetCtxRight = g_SlaveList[hitIndex];
                            isConnectedRight = targetCtxRight->connected;
                            isPausedRight = targetCtxRight->paused.load();
                        }
                    }
                    if (isConnectedRight && targetCtxRight) {
                        QAction* pauseAction = menu.addAction(isPausedRight ? T("继续") : T("暂停"));
                        connect(pauseAction, &QAction::triggered,[=]() {
                            targetCtxRight->paused = !isPausedRight;
                            this->update();
                        });
                        
                        QAction* wifiAction = menu.addAction(T("重连网络"));
                        connect(wifiAction, &QAction::triggered, [=]() {
                            if (targetCtxRight->sock != INVALID_SOCKET_HANDLE) {
                                char pkt = 20;
                                std::lock_guard<std::mutex> netLock(targetCtxRight->sendLock);
                                send(targetCtxRight->sock, &pkt, 1, 0);
                            }
                        });
                    }
                }
            }

            if (!isLocalRight && !isSlaveMode && hitIndex >= 0) {
                 bool isConnectedRight = true;
                 std::shared_ptr<SlaveCtx> targetCtxRight;
                 {
                     std::lock_guard<std::mutex> lock(g_SlaveListLock);
                     if (hitIndex < (int)g_SlaveList.size()) {
                         targetCtxRight = g_SlaveList[hitIndex];
                         isConnectedRight = targetCtxRight->connected;
                     }
                 }
                 
                 if (!isConnectedRight) {
                     QAction* reconnectAction = menu.addAction(T("重连"));
                     connect(reconnectAction, &QAction::triggered, [=]() {
                         if (g_MainObject) ((ControlWindow*)g_MainObject)->reconnectSlave(hitIndex);
                     });
                 } else if (targetCtxRight) {
                     QAction* disconnectAction = menu.addAction(T("断开连接"));
                     connect(disconnectAction, &QAction::triggered, [=]() {
                         if (targetCtxRight->sock != INVALID_SOCKET_HANDLE) {
                             MDC_LOG_INFO(LogTag::UI, "Disconnecting slave %d from menu", hitIndex);
                             char pkt = 8;
                             {
                                 std::lock_guard<std::mutex> netLock(targetCtxRight->sendLock);
                                 send(targetCtxRight->sock, &pkt, 1, 0);
                             }
                             targetCtxRight->connected = false;
                             NetUtils::ShutdownSocket(targetCtxRight->sock);
                         }
                     });
                 }
            }

            if (isLocalRight) {
                QAction* stopAction = menu.addAction(T("断开连接"));
                connect(stopAction, &QAction::triggered, [=]() {
                    if (g_MainObject) ((ControlWindow*)g_MainObject)->onStop();
                });
            }

            QAction* propAction = menu.addAction(T("属性"));
            connect(propAction, &QAction::triggered,[=]() {
                std::string info = T("无属性数据").toStdString();
                if (isLocalRight) {
                    info = g_MySysProps;
                } else if (isSlaveMode && isMasterRect) {
                    info = g_MasterSysProps;
                } else if (!isSlaveMode && hitIndex >= 0) {
                    std::lock_guard<std::mutex> lock(g_SlaveListLock);
                    if (hitIndex < (int)g_SlaveList.size()) info = g_SlaveList[hitIndex]->sysProps;
                } else if (isSlaveMode && hitIndex >= 0) {
                    std::lock_guard<std::mutex> lock(g_MirrorListLock);
                    if (hitIndex < (int)g_MirrorList.size()) info = g_MirrorList[hitIndex].sysProps;
                }
                
                QMessageBox::information(this, T("设备属性"), QString::fromStdString(info));
            });

            menu.exec(event->globalPosition().toPoint());
        }
    }
}

void MapWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_isPanning) {
        QPoint delta = event->pos() - m_panStartPos;
        m_viewOffset = m_panStartOffset + QPointF(delta.x(), delta.y());
        update();
        return;
    }

    int newHover = 0;
    if (m_barRect.contains(event->pos())) {
        if (m_lockBtnRect.contains(event->pos())) newHover = 1;
        else if (m_pauseBtnRect.contains(event->pos())) newHover = 2;
        else if (m_reconBtnRect.contains(event->pos())) newHover = 3;
        else if (m_wifiBtnRect.contains(event->pos())) newHover = 4;
        else if (m_stopBtnRect.contains(event->pos())) newHover = 6;
    } else if (m_focusBtnRect.contains(event->pos())) {
        newHover = 8;
    } else if (m_zoomInBtnRect.contains(event->pos())) {
        newHover = 9;
    } else if (m_zoomOutBtnRect.contains(event->pos())) {
        newHover = 10;
    } else if (m_addBtnRect.contains(event->pos())) {
        newHover = 7;
    } else if (m_settingsBtnRect.contains(event->pos())) {
        newHover = 5;
    }

    if (newHover != m_hoveredBtn) {
        m_hoveredBtn = newHover;
        if (newHover != 0) {
            QString tip;
            if (newHover == 7) tip = T("设备连接");
            else if (newHover == 5) tip = T("全局");
            else if (newHover == 1) tip = T("锁定/解锁设备");
            else if (newHover == 2) tip = T("暂停/继续设备");
            else if (newHover == 3) tip = T("重连网络控制通道");
            else if (newHover == 4) tip = T("重连文件传输通道");
            else if (newHover == 6) tip = T("断开连接");
            else if (newHover == 8) tip = T("居中视图");
            else if (newHover == 9) tip = T("放大视图");
            else if (newHover == 10) tip = T("缩小视图");
            
            QToolTip::showText(event->globalPosition().toPoint(), tip, this);
        } else {
            QToolTip::hideText();
        }
        update();
    }

    std::shared_ptr<SlaveCtx> ctx;
    {
        std::lock_guard<std::mutex> lock(g_SlaveListLock);
        if (m_isDragging && m_dragSlaveIdx >= 0 && m_dragSlaveIdx < (int)g_SlaveList.size()) {
            ctx = g_SlaveList[m_dragSlaveIdx];
        }
    }

    if (ctx) {
        update();

        int cw = width(), ch = height();
        double logicalMW = g_LocalW / g_LocalScale;
        double logicalMH = g_LocalH / g_LocalScale;
        double totalW = logicalMW * 3; 
        double totalH = logicalMH * 3;
        double s = std::min((cw - 2 * 40) / totalW, (ch - 2 * 40) / totalH) * m_zoomFactor;
        
        QPoint delta = event->pos() - m_dragStartPos;
        QPointF currentRemoteTopLeft = m_dragStartRemotePos + delta;

        double newLogicalX = (currentRemoteTopLeft.x() - m_currentLocalRect.left()) / s;
        double newLogicalY = (currentRemoteTopLeft.y() - m_currentLocalRect.top()) / s;

        double slaveLogW = ctx->width / ctx->scale;
        double slaveLogH = ctx->height / ctx->scale;

        std::vector<QRectF> snapTargets;
        snapTargets.push_back(QRectF(0, 0, logicalMW, logicalMH));
        
        {
            std::lock_guard<std::mutex> lock(g_SlaveListLock);
            for (size_t k = 0; k < g_SlaveList.size(); ++k) {
                if ((int)k == m_dragSlaveIdx || !g_SlaveList[k]->connected) continue;
                double ow = g_SlaveList[k]->width / g_SlaveList[k]->scale;
                double oh = g_SlaveList[k]->height / g_SlaveList[k]->scale;
                snapTargets.push_back(QRectF(g_SlaveList[k]->logicalX, g_SlaveList[k]->logicalY, ow, oh));
            }
        }

        double snapThreshold = 20.0 / s; 
        double bestDx = snapThreshold, bestDy = snapThreshold;
        
        QRectF movingRect(newLogicalX, newLogicalY, slaveLogW, slaveLogH);

        for (const auto& tr : snapTargets) {
            if (std::abs(movingRect.right() - tr.left()) < bestDx) { newLogicalX = tr.left() - slaveLogW; bestDx = std::abs(movingRect.right() - tr.left()); }
            if (std::abs(movingRect.left() - tr.right()) < bestDx) { newLogicalX = tr.right(); bestDx = std::abs(movingRect.left() - tr.right()); }
            if (std::abs(movingRect.left() - tr.left()) < bestDx) { newLogicalX = tr.left(); bestDx = std::abs(movingRect.left() - tr.left()); }
            if (std::abs(movingRect.right() - tr.right()) < bestDx) { newLogicalX = tr.right() - slaveLogW; bestDx = std::abs(movingRect.right() - tr.right()); }
            
            if (std::abs(movingRect.bottom() - tr.top()) < bestDy) { newLogicalY = tr.top() - slaveLogH; bestDy = std::abs(movingRect.bottom() - tr.top()); }
            if (std::abs(movingRect.top() - tr.bottom()) < bestDy) { newLogicalY = tr.bottom(); bestDy = std::abs(movingRect.top() - tr.bottom()); }
            if (std::abs(movingRect.top() - tr.top()) < bestDy) { newLogicalY = tr.top(); bestDy = std::abs(movingRect.top() - tr.top()); }
            if (std::abs(movingRect.bottom() - tr.bottom()) < bestDy) { newLogicalY = tr.bottom() - slaveLogH; bestDy = std::abs(movingRect.bottom() - tr.bottom()); }
        }

        movingRect.moveTo(newLogicalX, newLogicalY);

        bool collision = false;
        for (const auto& tr : snapTargets) {
            if (movingRect.adjusted(1, 1, -1, -1).intersects(tr)) {
                collision = true;
                break;
            }
        }

        if (!collision) {
            ctx->logicalX = newLogicalX;
            ctx->logicalY = newLogicalY;
        }
    }
}

void MapWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (m_isPanning) {
            m_isPanning = false;
            update();
        }

        if (m_isDragging && m_dragSlaveIdx >= 0) {
            std::shared_ptr<SlaveCtx> ctx;
            {
                std::lock_guard<std::mutex> lock(g_SlaveListLock);
                if (m_dragSlaveIdx < (int)g_SlaveList.size()) {
                    ctx = g_SlaveList[m_dragSlaveIdx];
                }
            }

            if (ctx && ctx->connected && ctx->sock != INVALID_SOCKET_HANDLE) {
                if (g_RememberPos) {
                    QSettings settings("MDControl", "DevicePositions");
                    QString key = QString::fromStdString(ctx->connectAddress) + (ctx->isBluetooth ? "_BT" : "_TCP");
                    settings.setValue(key + "_X", ctx->logicalX);
                    settings.setValue(key + "_Y", ctx->logicalY);
                }

                char buf[25];
                buf[0] = 10;
                unsigned int mW = htonl(g_LocalW);
                unsigned int mH = htonl(g_LocalH);
                memcpy(buf + 1, &mW, 4);
                memcpy(buf + 5, &mH, 4);
                double lX = ctx->logicalX;
                double lY = ctx->logicalY;
                memcpy(buf + 9, &lX, 8);
                memcpy(buf + 17, &lY, 8);
                
                std::lock_guard<std::mutex> lock(ctx->sendLock);
                send(ctx->sock, buf, 25, 0);
            }
            
            m_isDragging = false;
            m_dragSlaveIdx = -1;
            update();
        }
    }
}

void MapWidget::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        double angle = event->angleDelta().y();
        if (angle > 0) {
            m_zoomFactor *= 1.1;
        } else if (angle < 0) {
            m_zoomFactor /= 1.1;
        }
        if (m_zoomFactor < 0.2) m_zoomFactor = 0.2;
        if (m_zoomFactor > 5.0) m_zoomFactor = 5.0;
        update();
        event->accept();
    } else {
        QWidget::wheelEvent(event);
    }
}

void MapWidget::keyPressEvent(QKeyEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->key() == Qt::Key_Equal || event->key() == Qt::Key_Plus) {
            m_zoomFactor *= 1.2;
            if (m_zoomFactor > 5.0) m_zoomFactor = 5.0;
            update();
            event->accept();
            return;
        } else if (event->key() == Qt::Key_Minus) {
            m_zoomFactor /= 1.2;
            if (m_zoomFactor < 0.2) m_zoomFactor = 0.2;
            update();
            event->accept();
            return;
        }
    }

    bool isSlaveMode = (g_ClientSock != INVALID_SOCKET_HANDLE);
    if (!isSlaveMode && m_selectedDeviceIdx >= 0 && 
        (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down || 
         event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)) {
        
        std::shared_ptr<SlaveCtx> ctx;
        {
            std::lock_guard<std::mutex> lock(g_SlaveListLock);
            if (m_selectedDeviceIdx < (int)g_SlaveList.size()) {
                ctx = g_SlaveList[m_selectedDeviceIdx];
            }
        }

        if (ctx && ctx->connected && ctx->sock != INVALID_SOCKET_HANDLE) {
            double dx = 0;
            double dy = 0;
            if (event->key() == Qt::Key_Up) dy = -1.0;
            else if (event->key() == Qt::Key_Down) dy = 1.0;
            else if (event->key() == Qt::Key_Left) dx = -1.0;
            else if (event->key() == Qt::Key_Right) dx = 1.0;

            double newLogicalX = ctx->logicalX + dx;
            double newLogicalY = ctx->logicalY + dy;

            double slaveLogW = ctx->width / ctx->scale;
            double slaveLogH = ctx->height / ctx->scale;
            QRectF movingRect(newLogicalX, newLogicalY, slaveLogW, slaveLogH);

            double logicalMW = g_LocalW / g_LocalScale;
            double logicalMH = g_LocalH / g_LocalScale;
            std::vector<QRectF> snapTargets;
            snapTargets.push_back(QRectF(0, 0, logicalMW, logicalMH));
            
            {
                std::lock_guard<std::mutex> lock(g_SlaveListLock);
                for (size_t k = 0; k < g_SlaveList.size(); ++k) {
                    if ((int)k == m_selectedDeviceIdx || !g_SlaveList[k]->connected) continue;
                    double ow = g_SlaveList[k]->width / g_SlaveList[k]->scale;
                    double oh = g_SlaveList[k]->height / g_SlaveList[k]->scale;
                    snapTargets.push_back(QRectF(g_SlaveList[k]->logicalX, g_SlaveList[k]->logicalY, ow, oh));
                }
            }

            bool collision = false;
            for (const auto& tr : snapTargets) {
                if (movingRect.adjusted(1, 1, -1, -1).intersects(tr)) {
                    collision = true;
                    break;
                }
            }

            if (!collision) {
                ctx->logicalX = newLogicalX;
                ctx->logicalY = newLogicalY;

                if (g_RememberPos) {
                    QSettings settings("MDControl", "DevicePositions");
                    QString key = QString::fromStdString(ctx->connectAddress) + (ctx->isBluetooth ? "_BT" : "_TCP");
                    settings.setValue(key + "_X", ctx->logicalX);
                    settings.setValue(key + "_Y", ctx->logicalY);
                }

                char buf[25];
                buf[0] = 10;
                unsigned int mW = htonl(g_LocalW);
                unsigned int mH = htonl(g_LocalH);
                memcpy(buf + 1, &mW, 4);
                memcpy(buf + 5, &mH, 4);
                double lX = ctx->logicalX;
                double lY = ctx->logicalY;
                memcpy(buf + 9, &lX, 8);
                memcpy(buf + 17, &lY, 8);
                
                std::lock_guard<std::mutex> lock(ctx->sendLock);
                send(ctx->sock, buf, 25, 0);

                update();
            }
            event->accept();
            return;
        }
    }

    QWidget::keyPressEvent(event);
}