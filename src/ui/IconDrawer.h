#ifndef ICON_DRAWER_H
#define ICON_DRAWER_H

#include <QtGui/QPainter>
#include <QtCore/QRectF>
#include <QtGui/QColor>
#include <QtGui/QIcon>

namespace IconDrawer {
    void drawAddIcon(QPainter& painter, const QRectF& r);
    void drawMinusIcon(QPainter& painter, const QRectF& r); 
    void drawLockIcon(QPainter& painter, const QRectF& r, bool locked = true);
    void drawPausePlayIcon(QPainter& painter, const QRectF& r, bool isPaused);
    void drawReconnectIcon(QPainter& painter, const QRectF& r);
    void drawWifiIcon(QPainter& painter, const QRectF& r, bool failed, QColor color = Qt::white);
    void drawWifiFailedIcon(QPainter& painter, const QRectF& r);
    void drawSettingsIcon(QPainter& painter, const QRectF& r);
    void drawStopIcon(QPainter& painter, const QRectF& r);
    void drawDisconnectedIcon(QPainter& painter, const QRectF& r);
    void drawFocusIcon(QPainter& painter, const QRectF& r); 
    
    QIcon getDeviceIcon(bool isHistory, bool isPaired);
    QIcon getDeleteIcon();
    QIcon getTransferPauseIcon(bool isPaused);
    
    QIcon getAppIcon();
}

#endif // ICON_DRAWER_H