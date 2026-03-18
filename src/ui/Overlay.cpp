#include "Common.h"
#include <QtWidgets/QWidget>
#include <QtWidgets/QApplication>
#include <QtGui/QScreen>
#include <QtGui/QPainter>

class MaskWidget : public QWidget {
public:
    MaskWidget() {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        
        setAttribute(Qt::WA_TranslucentBackground);
        
        // Explicitly disable mouse event pass-through to block hover interactions on underlying windows
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        
        // Set blank cursor to hide mouse
        setCursor(Qt::BlankCursor);

        if (QScreen* screen = QApplication::primaryScreen()) {
            setGeometry(screen->geometry());
        }
    }
    
    // Draw background with slight Alpha (1/255) to pass Hit-Test while remaining visually transparent
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(0, 0, 0, 1));
    }
};

MaskWidget* g_MaskWidget = nullptr;

void InitOverlay() {
    if (!g_MaskWidget) {
        g_MaskWidget = new MaskWidget();
    }
}

void UpdateUI() {
    if (!g_MaskWidget) return;

    bool shouldHideCursor = false;

    if (g_ClientSock != INVALID_SOCKET_HANDLE) {
        // Slave mode
        // Focused -> Show mouse (Overlay Hide)
        // Unfocused -> Hide mouse (Overlay Show)
        if (!g_SlaveFocused) {
            shouldHideCursor = true;
        }
    } else {
        // Master mode
        // Remote control active -> Hide mouse (Overlay Show)
        // Local control active -> Show mouse (Overlay Hide)
        if (g_IsRemote) {
            shouldHideCursor = true;
        }
    }

    if (shouldHideCursor) {
        g_MaskWidget->show();
        g_MaskWidget->raise();
        g_MaskWidget->activateWindow();
    } else {
        g_MaskWidget->hide();
    }
}