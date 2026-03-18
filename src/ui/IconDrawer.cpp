#include "IconDrawer.h"
#include <QtGui/QPixmap>
#include <QtGui/QPainterPath>
#include <QtSvg/QSvgRenderer>
#include <QtCore/QByteArray>
#include <cmath>
#include <algorithm>

namespace IconDrawer {

    void drawAddIcon(QPainter& painter, const QRectF& r) {
        if (r.width() <= 0 || r.height() <= 0) return;
        qreal dpr = painter.device()->devicePixelRatioF();
        QPixmap pixmap(std::max(1, (int)(r.width() * dpr)), std::max(1, (int)(r.height() * dpr)));
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);

        QPainter tp(&pixmap);
        tp.setRenderHint(QPainter::Antialiasing);

        QRectF localR(0, 0, r.width(), r.height());
        double baseSize = std::min(localR.width(), localR.height());
        QPointF center = localR.center();

        double iconSize = baseSize * 0.95;
        QRectF iconRect(center.x() - iconSize / 2.0, center.y() - iconSize / 2.0, iconSize, iconSize);

        static QSvgRenderer renderer(QByteArray(R"(<svg viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg">
  <path d="M 512 256 v 512 M 256 512 h 512" fill="none" stroke="#ffffff" stroke-width="72" stroke-linecap="round" stroke-linejoin="round" />
</svg>)"));
        renderer.render(&tp, iconRect);

        painter.drawPixmap(r.topLeft(), pixmap);
    }

    void drawMinusIcon(QPainter& painter, const QRectF& r) {
        if (r.width() <= 0 || r.height() <= 0) return;
        qreal dpr = painter.device()->devicePixelRatioF();
        QPixmap pixmap(std::max(1, (int)(r.width() * dpr)), std::max(1, (int)(r.height() * dpr)));
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);

        QPainter tp(&pixmap);
        tp.setRenderHint(QPainter::Antialiasing);

        QRectF localR(0, 0, r.width(), r.height());
        double baseSize = std::min(localR.width(), localR.height());
        QPointF center = localR.center();

        double iconSize = baseSize * 0.95;
        QRectF iconRect(center.x() - iconSize / 2.0, center.y() - iconSize / 2.0, iconSize, iconSize);

        static QSvgRenderer renderer(QByteArray(R"(<svg viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg">
  <path d="M 256 512 h 512" fill="none" stroke="#ffffff" stroke-width="72" stroke-linecap="round" stroke-linejoin="round" />
</svg>)"));
        renderer.render(&tp, iconRect);

        painter.drawPixmap(r.topLeft(), pixmap);
    }

    void drawLockIcon(QPainter& painter, const QRectF& r, bool locked) {
        if (r.width() <= 0 || r.height() <= 0) return;
        qreal dpr = painter.device()->devicePixelRatioF();
        QPixmap pixmap(std::max(1, (int)(r.width() * dpr)), std::max(1, (int)(r.height() * dpr)));
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);

        QPainter tp(&pixmap);
        tp.setRenderHint(QPainter::Antialiasing);

        QRectF localR(0, 0, r.width(), r.height());
        double baseSize = std::min(localR.width(), localR.height());
        QPointF center = localR.center();

        double iconSize = baseSize * 0.95; 
        QRectF iconRect(center.x() - iconSize / 2.0, center.y() - iconSize / 2.0, iconSize, iconSize);

        if (locked) {
            static QSvgRenderer rendererLocked(QByteArray(R"(<svg viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg">
  <rect x="272" y="480" width="480" height="320" rx="64" fill="none" stroke="#ffffff" stroke-width="64" stroke-linecap="round" stroke-linejoin="round" />
  <path d="M 368 480 V 352 A 144 144 0 0 1 656 352 V 480" fill="none" stroke="#ffffff" stroke-width="64" stroke-linecap="round" stroke-linejoin="round" />
</svg>)"));
            rendererLocked.render(&tp, iconRect);
        } else {
            static QSvgRenderer rendererUnlocked(QByteArray(R"(<svg viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg">
  <rect x="272" y="480" width="480" height="320" rx="64" fill="none" stroke="#ffffff" stroke-width="64" stroke-linecap="round" stroke-linejoin="round" />
  <path d="M 368 480 V 352 A 144 144 0 0 1 656 352 V 350" fill="none" stroke="#ffffff" stroke-width="64" stroke-linecap="round" stroke-linejoin="round" />
</svg>)"));
            rendererUnlocked.render(&tp, iconRect);
        }

        painter.drawPixmap(r.topLeft(), pixmap);
    }

    void drawPausePlayIcon(QPainter& painter, const QRectF& r, bool isPaused) {
        if (r.width() <= 0 || r.height() <= 0) return;
        qreal dpr = painter.device()->devicePixelRatioF();
        QPixmap pixmap(std::max(1, (int)(r.width() * dpr)), std::max(1, (int)(r.height() * dpr)));
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);

        QPainter tp(&pixmap);
        tp.setRenderHint(QPainter::Antialiasing);

        QRectF localR(0, 0, r.width(), r.height());
        double baseSize = std::min(localR.width(), localR.height());
        QPointF center = localR.center();

        double iconSize = baseSize * 0.95; 
        QRectF iconRect(center.x() - iconSize / 2.0, center.y() - iconSize / 2.0, iconSize, iconSize);

        if (isPaused) {
            static QSvgRenderer rendererPlay(QByteArray(R"SVG(
                <svg viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg">
  <g transform="translate(102.4 102.4) scale(0.8)">
    <path d="M 352 192 L 832 512 L 352 832 Z" fill="none" stroke="#ffffff" stroke-width="64" stroke-linecap="round" stroke-linejoin="round" />
  </g>
</svg>
)SVG"));
            rendererPlay.render(&tp, iconRect);
        } else {
            static QSvgRenderer rendererPause(QByteArray(R"(<svg viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg">
  <rect x="284" y="224" width="70" height="576" rx="32" fill="#ffffff" stroke="#ffffff" stroke-width="64" stroke-linecap="round" stroke-linejoin="round" />
  <rect x="612" y="224" width="70" height="576" rx="32" fill="#ffffff" stroke="#ffffff" stroke-width="64" stroke-linecap="round" stroke-linejoin="round" />
</svg>)"));
            rendererPause.render(&tp, iconRect);
        }

        painter.drawPixmap(r.topLeft(), pixmap);
    }

    void drawReconnectIcon(QPainter& painter, const QRectF& r) {
        if (r.width() <= 0 || r.height() <= 0) return;
        qreal dpr = painter.device()->devicePixelRatioF();
        QPixmap pixmap(std::max(1, (int)(r.width() * dpr)), std::max(1, (int)(r.height() * dpr)));
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);

        QPainter tp(&pixmap);
        tp.setRenderHint(QPainter::Antialiasing);

        QRectF localR(0, 0, r.width(), r.height());
        double baseSize = std::min(localR.width(), localR.height());
        QPointF center = localR.center();

        double iconSize = baseSize * 0.95;
        QRectF iconRect(center.x() - iconSize / 2.0, center.y() - iconSize / 2.0, iconSize, iconSize);

        static QSvgRenderer renderer(QByteArray(R"SVG(
<svg viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg">
  <g transform="translate(512 512) scale(-0.85 0.85) translate(-512 -512)">
    <path d="M278.209 198.442l6.069 525.867c0.319 27.612 22.961 49.738 50.574 49.42a50 50 0 0 0 32.173-12.216l127.94-110.903 130.458 225.959c13.807 23.914 44.386 32.108 68.3 18.301l54.816-31.648c23.915-13.807 32.108-44.386 18.301-68.3l-130.457-225.96 160.128-55.387c26.097-9.026 39.935-37.5 30.908-63.597a50 50 0 0 0-21.718-26.644L353.74 154.877c-23.742-14.102-54.42-6.288-68.523 17.454a50 50 0 0 0-7.008 26.111z"
      fill="none"
      stroke="#ffffff"
      stroke-width="64"
      stroke-linecap="round"
      stroke-linejoin="round"/>
  </g>
</svg>
)SVG"));
        renderer.render(&tp, iconRect);

        painter.drawPixmap(r.topLeft(), pixmap);
    }

    void drawWifiIcon(QPainter& painter, const QRectF& r, bool failed, QColor color) {
        if (r.width() <= 0 || r.height() <= 0) return;
        qreal dpr = painter.device()->devicePixelRatioF();
        QPixmap pixmap(std::max(1, (int)(r.width() * dpr)), std::max(1, (int)(r.height() * dpr)));
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);

        QPainter tp(&pixmap);
        tp.setRenderHint(QPainter::Antialiasing);

        QRectF localR(0, 0, r.width(), r.height());
        double baseSize = std::min(localR.width(), localR.height());
        QPointF center = localR.center();

        double iconSize = baseSize * 0.95; 
        QRectF iconRect(center.x() - iconSize / 2.0, center.y() - iconSize / 2.0, iconSize, iconSize);

        if (color == Qt::white) {
            static QSvgRenderer rendererWhite(QByteArray(R"(<svg viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg"><path d="M 454 743 H 224 V 282 H 420 L 512 374 H 800 V 486 M 624 743 v 1 M 552 671 A 102 102 0 0 1 696 671 M 480 599 A 204 204 0 0 1 768 601" fill="none" stroke="#ffffff" stroke-width="64" stroke-linecap="round" stroke-linejoin="round"/></svg>)"));
            rendererWhite.render(&tp, iconRect);
        } else if (color == Qt::black) {
            static QSvgRenderer rendererBlack(QByteArray(R"(<svg viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg"><path d="M 454 743 H 224 V 282 H 420 L 512 374 H 800 V 486 M 624 743 v 1 M 552 671 A 102 102 0 0 1 696 671 M 480 599 A 204 204 0 0 1 768 601" fill="none" stroke="#000000" stroke-width="64" stroke-linecap="round" stroke-linejoin="round"/></svg>)"));
            rendererBlack.render(&tp, iconRect);
        } else {
            QString svgStr = QString(R"(<svg viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg"><path d="M 454 743 H 224 V 282 H 420 L 512 374 H 800 V 486 M 624 743 v 1 M 552 671 A 102 102 0 0 1 696 671 M 480 599 A 204 204 0 0 1 768 601" fill="none" stroke="%1" stroke-width="64" stroke-linecap="round" stroke-linejoin="round"/></svg>)").arg(color.name());
            QSvgRenderer renderer(svgStr.toUtf8());
            renderer.render(&tp, iconRect);
        }

        painter.drawPixmap(r.topLeft(), pixmap);
    }

    void drawWifiFailedIcon(QPainter& painter, const QRectF& r) {
        if (r.width() <= 0 || r.height() <= 0) return;

        qreal dpr = painter.device()->devicePixelRatioF();
        QPixmap pixmap(std::max(1, (int)(r.width() * dpr)), std::max(1, (int)(r.height() * dpr)));
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);

        QPainter tp(&pixmap);
        tp.setRenderHint(QPainter::Antialiasing);

        tp.setPen(Qt::NoPen);
        tp.setBrush(QColor(0, 0, 0, 150));
        tp.drawRoundedRect(QRectF(0, 0, r.width(), r.height()), 4, 4);

        QRectF localR(0, 0, r.width(), r.height());
        double baseSize = std::min(localR.width(), localR.height());
        QPointF center = localR.center();

        double iconSize = baseSize * 1;
        QRectF iconRect(center.x() - iconSize / 2.0, center.y() - iconSize / 2.0, iconSize, iconSize);

        static QSvgRenderer renderer(QByteArray(
            R"(<svg viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg">)"
            R"(<path d="M301.632 602.304a45.632 45.632 0 0 0 65.856 0 200.576 200.576 0 0 1 289.088 0 45.632 45.632 0 0 0 65.92 0 48.448 48.448 0 0 0 13.632-33.92c0-12.672-4.928-24.96-13.696-33.856a291.968 291.968 0 0 0-420.8 0 48.512 48.512 0 0 0-13.632 33.92c0 12.736 4.928 24.96 13.632 33.92z m-160-220.48a48.64 48.64 0 0 0-13.632 33.92c0 12.736 4.928 24.96 13.632 33.92a45.632 45.632 0 0 0 65.856 0 422.528 422.528 0 0 1 609.024 0 45.632 45.632 0 0 0 65.92 0 48.576 48.576 0 0 0 13.568-33.92 48.576 48.576 0 0 0-13.632-33.92C783.488 280.128 651.968 224 512 224c-139.904 0-271.424 56.064-370.368 157.888zM512 800c35.968 0 65.088-30.016 65.088-67.008S547.968 665.984 512 665.984c-35.904 0-65.088 30.016-65.088 67.008S476.032 800 512 800z" fill="#ffffff"/>)"
            R"(<path d="M230.24166 158.764939m32.735921 35.104978l501.950793 538.276324q32.735921 35.104978-2.369056 67.840899l0 0q-35.104978 32.735921-67.840899-2.369057l-501.950793-538.276324q-32.735921-35.104978 2.369056-67.840899l0 0q35.104978-32.735921 67.840899 2.369057Z" fill="#ffffff"/>)"
            R"(</svg>)"
        ));
        renderer.render(&tp, iconRect);

        painter.drawPixmap(r.topLeft(), pixmap);
    }

    void drawSettingsIcon(QPainter& painter, const QRectF& r) {
        if (r.width() <= 0 || r.height() <= 0) return;
        qreal dpr = painter.device()->devicePixelRatioF();
        QPixmap pixmap(std::max(1, (int)(r.width() * dpr)), std::max(1, (int)(r.height() * dpr)));
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);

        QPainter tp(&pixmap);
        tp.setRenderHint(QPainter::Antialiasing);

        QRectF localR(0, 0, r.width(), r.height());
        double baseSize = std::min(localR.width(), localR.height());
        QPointF center = localR.center();

        double iconSize = baseSize * 0.95; 
        QRectF iconRect(center.x() - iconSize / 2.0, center.y() - iconSize / 2.0, iconSize, iconSize);

        static QSvgRenderer renderer(QByteArray(R"(<svg viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg">
  <path d="M 512 224 L 761.4 368 L 761.4 656 L 512 800 L 262.6 656 L 262.6 368 Z" fill="none" stroke="#ffffff" stroke-width="64" stroke-linecap="round" stroke-linejoin="round" />
  <circle cx="512" cy="512" r="96" fill="none" stroke="#ffffff" stroke-width="64" stroke-linecap="round" stroke-linejoin="round" />
</svg>)"));
        renderer.render(&tp, iconRect);

        painter.drawPixmap(r.topLeft(), pixmap);
    }

    void drawStopIcon(QPainter& painter, const QRectF& r) {
        if (r.width() <= 0 || r.height() <= 0) return;
        qreal dpr = painter.device()->devicePixelRatioF();
        QPixmap pixmap(std::max(1, (int)(r.width() * dpr)), std::max(1, (int)(r.height() * dpr)));
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);

        QPainter tp(&pixmap);
        tp.setRenderHint(QPainter::Antialiasing);

        QRectF localR(0, 0, r.width(), r.height());
        double baseSize = std::min(localR.width(), localR.height());
        QPointF center = localR.center();

        double iconSize = baseSize * 0.85; 
        QRectF iconRect(center.x() - iconSize / 2.0, center.y() - iconSize / 2.0, iconSize, iconSize);

        static QSvgRenderer renderer(QByteArray(R"(<svg viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg"><rect x="224" y="224" width="576" height="576" rx="64" fill="none" stroke="#ffffff" stroke-width="64" stroke-linecap="round" stroke-linejoin="round" /></svg>)"));
        renderer.render(&tp, iconRect);

        painter.drawPixmap(r.topLeft(), pixmap);
    }

    void drawDisconnectedIcon(QPainter& painter, const QRectF& r) {
        if (r.width() <= 0 || r.height() <= 0) return;
        qreal dpr = painter.device()->devicePixelRatioF();
        QPixmap pixmap(std::max(1, (int)(r.width() * dpr)), std::max(1, (int)(r.height() * dpr)));
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);

        QPainter tp(&pixmap);
        tp.setRenderHint(QPainter::Antialiasing);

        QRectF localR(0, 0, r.width(), r.height());
        double baseSize = std::min(localR.width(), localR.height());
        QPointF center = localR.center();

        double iconSize = baseSize * 0.95; 
        QRectF iconRect(center.x() - iconSize / 2.0, center.y() - iconSize / 2.0, iconSize, iconSize);

        static QSvgRenderer renderer(QByteArray(R"(<svg viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg">
  <path d="M 400 350 H 250 A 150 150 0 0 0 250 650 H 400" fill="none" stroke="#ffffff" stroke-width="64" stroke-linecap="round" stroke-linejoin="round"/>
  <path d="M 600 350 H 750 A 150 150 0 0 1 750 650 H 600" fill="none" stroke="#ffffff" stroke-width="64" stroke-linecap="round" stroke-linejoin="round"/>
  <path d="M 350 750 L 650 250" fill="none" stroke="#ffffff" stroke-width="64" stroke-linecap="round" stroke-linejoin="round"/>
</svg>)"));
        renderer.render(&tp, iconRect);

        painter.drawPixmap(r.topLeft(), pixmap);
    }
    
    void drawFocusIcon(QPainter& painter, const QRectF& r) {
        if (r.width() <= 0 || r.height() <= 0) return;
        qreal dpr = painter.device()->devicePixelRatioF();
        QPixmap pixmap(std::max(1, (int)(r.width() * dpr)), std::max(1, (int)(r.height() * dpr)));
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);

        QPainter tp(&pixmap);
        tp.setRenderHint(QPainter::Antialiasing);

        QRectF localR(0, 0, r.width(), r.height());
        double baseSize = std::min(localR.width(), localR.height());
        QPointF center = localR.center();

        double iconSize = baseSize * 0.95;
        QRectF iconRect(center.x() - iconSize / 2.0, center.y() - iconSize / 2.0, iconSize, iconSize);

        static QSvgRenderer renderer(QByteArray(R"(<svg viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg">
  <circle cx="512" cy="512" r="384" fill="none" stroke="#ffffff" stroke-width="72" stroke-linecap="round" stroke-linejoin="round" />
  <circle cx="512" cy="512" r="128" fill="#ffffff" />
</svg>)"));
        renderer.render(&tp, iconRect);

        painter.drawPixmap(r.topLeft(), pixmap);
    }

    QIcon getBtDeviceIcon(bool isHistory, bool isPaired) {
        int iconW = 24; 
        
        if (!isHistory && !isPaired) {
            QPixmap emptyPix(1, iconW);
            emptyPix.fill(Qt::transparent);
            return QIcon(emptyPix);
        }

        int width = 0;
        if (isHistory) width += iconW;
        if (isPaired) width += iconW;
        
        QPixmap pixmap(width, iconW);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        
        int currentX = 0;
        
        if (isPaired) {
            QRectF rect(currentX, 0, iconW, iconW);
            static QSvgRenderer rendererPaired(QByteArray(R"(<svg viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg">
    <path d="M 400 350 H 250 A 150 150 0 0 0 250 650 H 400" fill="none" stroke="#ffffff" stroke-width="64" stroke-linecap="round" stroke-linejoin="round"/>
    <path d="M 600 350 H 750 A 150 150 0 0 1 750 650 H 600" fill="none" stroke="#ffffff" stroke-width="64" stroke-linecap="round" stroke-linejoin="round"/>
    <path d="M 400 500 H 600" fill="none" stroke="#ffffff" stroke-width="64" stroke-linecap="round" stroke-linejoin="round"/>
</svg>)"));
            rendererPaired.render(&p, rect.adjusted(2, 2, -2, -2));
            currentX += iconW;
        }
        
        if (isHistory) {
            QRectF rect(currentX, 0, iconW, iconW);
            static QSvgRenderer rendererHistory(QByteArray(R"(<svg viewBox="0 0 1024 1024" version="1.1" xmlns="http://www.w3.org/2000/svg"><path d="M974.754909 315.345455c108.613818 232.913455 7.842909 509.789091-225.093818 618.402909A465.454545 465.454545 0 0 1 230.4 847.546182a46.545455 46.545455 0 0 1 64.488727-67.118546 372.363636 372.363636 0 0 0 415.418182 68.957091c186.344727-86.877091 266.961455-308.363636 180.084364-494.708363C803.490909 168.331636 582.004364 87.738182 395.636364 174.615273a371.805091 371.805091 0 0 0-188.462546 199.377454l63.069091 5.306182a23.272727 23.272727 0 0 1 17.198546 36.468364l-123.857455 178.269091a46.312727 46.312727 0 0 1-41.588364 19.781818 41.634909 41.634909 0 0 1-35.770181-26.926546L12.893091 391.051636a23.272727 23.272727 0 0 1 23.738182-31.371636l74.472727 6.260364A464.896 464.896 0 0 1 356.305455 90.251636C589.265455-18.385455 866.141091 82.385455 974.754909 315.322182zM529.733818 232.727273a46.545455 46.545455 0 0 1 46.405818 43.054545l0.139637 3.490909v228.887273l139.473454 79.802182a46.545455 46.545455 0 0 1 18.967273 60.276363l-1.675636 3.258182a46.545455 46.545455 0 0 1-60.276364 18.944l-3.258182-1.675636-151.179636-86.481455a69.818182 69.818182 0 0 1-35.002182-55.924363l-0.139636-4.677818V279.272727a46.545455 46.545455 0 0 1 46.545454-46.545454z" fill="#ffffff" /></svg>)"));
            rendererHistory.render(&p, rect.adjusted(2, 2, -2, -2));
        }
        
        return QIcon(pixmap);
    }

    QIcon getDeleteIcon() {
        int iconW = 24;
        QPixmap pixmap(iconW, iconW);
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF rect(0, 0, iconW, iconW);
        static QSvgRenderer renderer(QByteArray(R"(<svg viewBox="0 0 1024 1024" xmlns="http://www.w3.org/2000/svg"><path d="M 256 256 L 768 768 M 768 256 L 256 768" fill="none" stroke="#ffffff" stroke-width="72" stroke-linecap="round" stroke-linejoin="round" /></svg>)"));
        renderer.render(&p, rect.adjusted(2, 2, -2, -2));
        return QIcon(pixmap);
    }
    
    QIcon getAppIcon() {
        QByteArray svgData = 
            "<svg viewBox=\"0 0 1750 1750\" xmlns=\"http://www.w3.org/2000/svg\">\n"
            "  <g transform=\"translate(150,-25)\">\n"
            "    \n"
            "    <g stroke=\"#8B5CF6\" stroke-width=\"32\" stroke-linejoin=\"round\">\n"
            "      \n"
            "      <polygon points=\"1225,220 1475,470 1475,1070 1225,820\" fill=\"#D6C500\" />\n"
            "      \n"
            "      <polygon points=\"1225,820 1475,1070 1475,1670 1225,1420\" fill=\"#1C8FE3\" />\n"
            "      \n"
            "      <polygon points=\"25,1420 1225,1420 1475,1670 275,1670\" fill=\"#1C8FE3\" />\n"
            "\n"
            "      <rect x=\"25\" y=\"220\" width=\"1200\" height=\"600\" fill=\"#FFEA00\"/>\n"
            "\n"
            "      <rect x=\"25\" y=\"820\" width=\"1200\" height=\"600\" fill=\"#2EA8FF\"/>\n"
            "    </g>\n"
            "\n"
            "    <g transform=\"translate(75,250) translate(512 512) rotate(-60) scale(-1.75 1.75) translate(-512 -512)\">\n"
            "      <path d=\"M278.209 198.442l6.069 525.867c0.319 27.612 22.961 49.738 50.574 49.42a50 50 0 0 0 32.173-12.216l127.94-110.903 130.458 225.959c13.807 23.914 44.386 32.108 68.3 18.301l54.816-31.648c23.915-13.807 32.108-44.386 18.301-68.3l-130.457-225.96 160.128-55.387c26.097-9.026 39.935-37.5 30.908-63.597a50 50 0 0 0-21.718-26.644L353.74 154.877c-23.742-14.102-54.42-6.288-68.523 17.454a50 50 0 0 0-7.008 26.111z\"\n"
            "        fill=\"rgba(0, 0, 0, 0.3)\"\n"
            "        stroke=\"#ffffff\"\n"
            "        stroke-width=\"64\"\n"
            "        stroke-linecap=\"round\"\n"
            "        stroke-linejoin=\"round\"/>\n"
            "    </g>\n"
            "\n"
            "  </g>\n"
            "\n"
            "</svg>";
            
        QSvgRenderer renderer(svgData);
        QPixmap pixmap(256, 256);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter);
        return QIcon(pixmap);
    }
}