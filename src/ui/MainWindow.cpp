#include "MainWindow.h"
#include "Common.h"
#include "SystemUtils.h"
#include "KvmContext.h"
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>
#include <QtCore/QTimer>
#include <QMimeData>
#include <QtCore/QUrl>
#include <debugapi.h>
#include <cstdarg>
#include <QtCore/QPointer>
#include <thread>
#include "KvmEvents.h"
#include <future>
#include <QtWidgets/QDialogButtonBox>
#include <QtCore/QThread>
#include <utility>
#include "IconDrawer.h"
#include <QtWidgets/QComboBox>
#include <QtWidgets/QKeySequenceEdit>
#include <QtWidgets/QFileDialog>
#include <QtGui/QDesktopServices>
#include <QtWidgets/QListView>
#include <QtCore/QDateTime>
#include <QtGui/QPainter>
#include <QtWidgets/QStyleOptionButton>
#include <QtWidgets/QStyle>

template<typename F>
void RunInQThread(F&& f) {
    QThread* t = QThread::create(std::forward<F>(f));
    QObject::connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

SocketHandle g_TcpListenSock = INVALID_SOCKET_HANDLE;

// --- Auth Helper ---

// Custom synchronizer to replace std::promise which has race condition bugs in MSVC
struct PinSync {
    std::mutex mtx;
    std::condition_variable cv;
    std::string pin;
    bool done = false;
};

bool recvAll(SocketHandle sock, char* buf, int size) {
    int total = 0;
    while(total < size) {
        int r = recv(sock, buf + total, size - total, 0);
        if (r <= 0) return false;
        total += r;
    }
    return true;
}

void SaveTrustedKey(SocketHandle sock, const std::string& key) {
    std::string peerId = SystemUtils::GetPeerAddress(sock);
    auto updateSettings = [key, peerId, sock]() {
        QSettings settings("MDControl", "Auth");
        QStringList trustedKeys = settings.value("TrustedKeys").toStringList();
        if (!trustedKeys.contains(QString::fromStdString(key))) {
            trustedKeys.append(QString::fromStdString(key));
            settings.setValue("TrustedKeys", trustedKeys);
        }
        QMap<QString, QVariant> peerToKey = settings.value("PeerToKey").toMap();
        peerToKey[QString::fromStdString(peerId)] = QString::fromStdString(key);
        settings.setValue("PeerToKey", peerToKey);

        if (SystemUtils::IsBluetoothSocket(sock)) {
            QSettings devSettings("MDControl", "Devices");
            QStringList paired = devSettings.value("PairedMACs").toStringList();
            try {
                unsigned long long addr = std::stoull(peerId, nullptr, 16);
                QString addrStr = QString::number(addr);
                if (!paired.contains(addrStr)) {
                    paired.append(addrStr);
                    devSettings.setValue("PairedMACs", paired);
                }
            } catch(...) {}
        }
    };

    if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
        updateSettings();
    } else {
        QMetaObject::invokeMethod(g_MainObject, updateSettings, Qt::BlockingQueuedConnection);
    }
}

static QString ShowPinDialog(const QString& name) {
    QDialog dlg(static_cast<QWidget*>(g_MainObject));
    dlg.setWindowTitle(T("配对验证"));
    dlg.setFixedSize(300, 130);
    
    QVBoxLayout* l = new QVBoxLayout(&dlg);
    
    QLabel* lbl = new QLabel(T("请确认并在两端输入相同的6位数字PIN码\n与设备 [%1]配对:").arg(name));
    lbl->setWordWrap(true);
    l->addWidget(lbl);
    
    QLineEdit* le = new QLineEdit();
    le->setMaxLength(6);
    l->addWidget(le);
    
    QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    l->addWidget(box);
    
    QObject::connect(box, &QDialogButtonBox::accepted, [&dlg, le]() {
        if (le->text().length() == 6) {
            dlg.accept();
        } else {
            QMessageBox::warning(&dlg, T("提示"), T("请输入6位数字PIN码"));
        }
    });
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    
    QTimer::singleShot(5 * 60 * 1000, &dlg, &QDialog::reject);
    
    if (dlg.exec() == QDialog::Accepted) {
        return le->text();
    }
    return QString();
}

std::string GetPinInputHelper(const QString& name) {
    MDC_LOG_INFO(LogTag::AUTH, "Requesting PIN input for device");
    if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
        QString text = ShowPinDialog(name);
        MDC_LOG_INFO(LogTag::AUTH, "Local PIN input completed length: %d", text.length());
        if (text.length() == 6) return text.toStdString();
        return "";
    } else {
        auto sync = std::make_shared<PinSync>();
        QCoreApplication::postEvent(g_MainObject, new AuthRequirePinEvent(new std::shared_ptr<PinSync>(sync), name));
        
        std::unique_lock<std::mutex> lock(sync->mtx);
        sync->cv.wait(lock,[&]() { return sync->done; });
        
        MDC_LOG_INFO(LogTag::AUTH, "Background thread received PIN input result");
        return sync->pin;
    }
}

// Symmetrical authentication core logic
bool PerformSymmetricAuth(SocketHandle sock, const std::string& targetName) {
    MDC_LOG_INFO(LogTag::AUTH, "Starting strict symmetrical auth for target");
    
    std::vector<char> pkt40;
    pkt40.push_back(40);
    uint32_t myKeyLen = htonl(g_MyPubKey.size());
    pkt40.insert(pkt40.end(), (char*)&myKeyLen, (char*)&myKeyLen + 4);
    pkt40.insert(pkt40.end(), g_MyPubKey.begin(), g_MyPubKey.end());
    if (!NetUtils::SendAll(sock, pkt40.data(), pkt40.size())) return false;

    char flag40;
    if (recv(sock, &flag40, 1, 0) <= 0 || flag40 != 40) return false;
    uint32_t peerKeyLen;
    if (!recvAll(sock, (char*)&peerKeyLen, 4)) return false;
    peerKeyLen = ntohl(peerKeyLen);
    std::vector<char> peerKeyData(peerKeyLen);
    if (!recvAll(sock, peerKeyData.data(), peerKeyLen)) return false;
    std::string peerPubKey(peerKeyData.begin(), peerKeyData.end());

    std::string peerId = SystemUtils::GetPeerAddress(sock);
    bool iTrustPeer = false;
    if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
        QSettings settings("MDControl", "Auth");
        QMap<QString, QVariant> peerToKey = settings.value("PeerToKey").toMap();
        if (peerToKey.value(QString::fromStdString(peerId)).toString().toStdString() == peerPubKey) {
            iTrustPeer = true;
        }
    } else {
        QMetaObject::invokeMethod(g_MainObject, [&iTrustPeer, peerId, peerPubKey]() {
            QSettings settings("MDControl", "Auth");
            QMap<QString, QVariant> peerToKey = settings.value("PeerToKey").toMap();
            if (peerToKey.value(QString::fromStdString(peerId)).toString().toStdString() == peerPubKey) {
                iTrustPeer = true;
            }
        }, Qt::BlockingQueuedConnection);
    }

    char myTrust = iTrustPeer ? 1 : 0;
    char pkt41[2] = {41, myTrust};
    if (!NetUtils::SendAll(sock, pkt41, 2)) return false;

    char flag41, peerTrust;
    if (recv(sock, &flag41, 1, 0) <= 0 || flag41 != 41) return false;
    if (recv(sock, &peerTrust, 1, 0) <= 0) return false;

    bool mutualTrust = (iTrustPeer && peerTrust == 1);

    if (mutualTrust) {
        MDC_LOG_INFO(LogTag::AUTH, "Mutual trust established proceeding to strict RSA challenge");
        std::string myNonce = g_Context->CryptoMgr->GenerateRandomString(32);
        std::string encMyNonce = g_Context->CryptoMgr->RSAEncrypt(peerPubKey, myNonce);
        
        std::vector<char> pkt47;
        pkt47.push_back(47);
        uint32_t encLen = htonl(encMyNonce.size());
        pkt47.insert(pkt47.end(), (char*)&encLen, (char*)&encLen + 4);
        pkt47.insert(pkt47.end(), encMyNonce.begin(), encMyNonce.end());
        if (!NetUtils::SendAll(sock, pkt47.data(), pkt47.size())) return false;

        char flag47;
        if (recv(sock, &flag47, 1, 0) <= 0 || flag47 != 47) return false;
        uint32_t peerEncLen;
        if (!recvAll(sock, (char*)&peerEncLen, 4)) return false;
        peerEncLen = ntohl(peerEncLen);
        std::vector<char> peerEncData(peerEncLen);
        if (!recvAll(sock, peerEncData.data(), peerEncLen)) return false;

        std::string decPeerNonce = g_Context->CryptoMgr->RSADecrypt(g_MyPrivKey, std::string(peerEncData.begin(), peerEncData.end()));

        std::vector<char> pkt48;
        pkt48.push_back(48);
        uint32_t decLen = htonl(decPeerNonce.size());
        pkt48.insert(pkt48.end(), (char*)&decLen, (char*)&decLen + 4);
        pkt48.insert(pkt48.end(), decPeerNonce.begin(), decPeerNonce.end());
        if (!NetUtils::SendAll(sock, pkt48.data(), pkt48.size())) return false;

        char flag48;
        if (recv(sock, &flag48, 1, 0) <= 0 || flag48 != 48) return false;
        uint32_t peerDecLen;
        if (!recvAll(sock, (char*)&peerDecLen, 4)) return false;
        peerDecLen = ntohl(peerDecLen);
        std::vector<char> peerDecData(peerDecLen);
        if (!recvAll(sock, peerDecData.data(), peerDecLen)) return false;
        std::string returnedMyNonce(peerDecData.begin(), peerDecData.end());

        if (returnedMyNonce == myNonce) {
            MDC_LOG_INFO(LogTag::AUTH, "RSA challenge successful both sides proved private key ownership");
            return true;
        } else {
            MDC_LOG_WARN(LogTag::AUTH, "RSA challenge failed decrypted nonce mismatch");
            return false;
        }
    } else {
        MDC_LOG_INFO(LogTag::AUTH, "Trust missing or asymmetrical falling back to explicit PIN pairing");
        std::string myPin = GetPinInputHelper(QString::fromStdString(targetName));
        if (myPin.empty()) {
            MDC_LOG_INFO(LogTag::AUTH, "PIN input cancelled");
            return false;
        }

        std::string encMyPin = g_Context->CryptoMgr->RSAEncrypt(peerPubKey, myPin);
        std::vector<char> pkt44;
        pkt44.push_back(44);
        uint32_t encLen = htonl(encMyPin.size());
        pkt44.insert(pkt44.end(), (char*)&encLen, (char*)&encLen + 4);
        pkt44.insert(pkt44.end(), encMyPin.begin(), encMyPin.end());
        if (!NetUtils::SendAll(sock, pkt44.data(), pkt44.size())) return false;

        char flag44;
        if (recv(sock, &flag44, 1, 0) <= 0 || flag44 != 44) return false;
        uint32_t peerEncLen;
        if (!recvAll(sock, (char*)&peerEncLen, 4)) return false;
        peerEncLen = ntohl(peerEncLen);
        std::vector<char> peerEncData(peerEncLen);
        if (!recvAll(sock, peerEncData.data(), peerEncLen)) return false;

        std::string decPeerPin = g_Context->CryptoMgr->RSADecrypt(g_MyPrivKey, std::string(peerEncData.begin(), peerEncData.end()));

        if (decPeerPin == myPin) {
            MDC_LOG_INFO(LogTag::AUTH, "PIN match successful saving trusted key for future RSA challenges");
            SaveTrustedKey(sock, peerPubKey);
            return true;
        } else {
            MDC_LOG_WARN(LogTag::AUTH, "PIN match failed");
            return false;
        }
    }
}

bool AuthMaster(SocketHandle sock, const std::string& targetName) {
    return PerformSymmetricAuth(sock, targetName);
}

bool AuthSlave(SocketHandle sock) {
    return PerformSymmetricAuth(sock, T("主控端").toStdString());
}

// --- ControlWindow Implementation ---

ControlWindow::ControlWindow() {
    qApp->setQuitOnLastWindowClosed(false);

    setWindowTitle("MDControl");
    
    qreal dpr = this->devicePixelRatioF();
    if (dpr <= 0.0) dpr = 1.0;
    resize(static_cast<int>(g_LocalW / (2.0 * dpr)), static_cast<int>(g_LocalH / (2.0 * dpr)));
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    
    mapWidget = new MapWidget();
    mapWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mapWidget->setFocusPolicy(Qt::StrongFocus); 
    mainLayout->addWidget(mapWidget);

    m_trayIcon = new QSystemTrayIcon(IconDrawer::getAppIcon(), this);
    m_trayMenu = new QMenu(this);
    connect(m_trayMenu, &QMenu::aboutToShow, this, &ControlWindow::updateTrayMenu);
    m_trayIcon->setContextMenu(m_trayMenu);

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason){
        MDC_LOG_INFO(LogTag::UI, "Tray icon activated reason: %d", reason);
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            this->showNormal();
            this->activateWindow();
        }
    });
    m_trayIcon->show();

    m_connectWindow = new QWidget(this, Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    m_connectWindow->setWindowTitle(T("设备连接"));
    
    QVBoxLayout* leftLayout = new QVBoxLayout(m_connectWindow);
    leftLayout->setContentsMargins(15, 15, 15, 15);
    leftLayout->setSizeConstraint(QLayout::SetFixedSize); 
    
    QHBoxLayout* connectLayout = new QHBoxLayout();
    m_lblConnectAsMaster = new QLabel(T("作为主控端连接"));
    connectLayout->addWidget(m_lblConnectAsMaster);
    modeTcpBtn = new QRadioButton(T("网络"));
    modeBtBtn = new QRadioButton(T("蓝牙"));
    modeTcpBtn->setChecked(true);
    connectLayout->addWidget(modeTcpBtn);
    connectLayout->addWidget(modeBtBtn);
    connectLayout->addStretch();
    leftLayout->addLayout(connectLayout);

    tcpConfigWidget = new QWidget();
    QHBoxLayout* netLayout = new QHBoxLayout(tcpConfigWidget);
    netLayout->setContentsMargins(0, 0, 0, 0);
    ipEdit = new QLineEdit(QString::fromStdString(g_TargetIP));
    portEdit = new QLineEdit(QString::number(g_TargetPort));
    m_lblIp = new QLabel(T("IP:"));
    netLayout->addWidget(m_lblIp); netLayout->addWidget(ipEdit);
    m_lblPort = new QLabel(T("端口:"));
    netLayout->addWidget(m_lblPort); netLayout->addWidget(portEdit);
    leftLayout->addWidget(tcpConfigWidget);

    btListWidget = new QListWidget();
    btListWidget->setFixedHeight(150);
    btListWidget->setMinimumWidth(380); 
    btListWidget->setIconSize(QSize(48, 24));
    btListWidget->hide(); 
    leftLayout->addWidget(btListWidget);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    startBtn = new QPushButton(T("连接")); 
    startBtn->setFixedWidth(120);
    btnLayout->addStretch();
    btnLayout->addWidget(startBtn);
    btnLayout->addStretch();
    leftLayout->addLayout(btnLayout);

    QHBoxLayout* listenLayout = new QHBoxLayout();
    m_lblListenAsMaster = new QLabel(T("监听其它主控端连接"));
    listenLayout->addWidget(m_lblListenAsMaster);
    listenTcpCb = new QCheckBox("TCP");
    listenTcpCb->setChecked(true);
    listenPortEdit = new QLineEdit(QString::number(g_TargetPort));
    listenPortEdit->setFixedWidth(50);
    listenBtCb = new QCheckBox(T("蓝牙"));
    listenBtCb->setChecked(true);
    listenLayout->addWidget(listenTcpCb);
    listenLayout->addWidget(listenPortEdit);
    listenLayout->addWidget(listenBtCb);
    listenLayout->addStretch();
    leftLayout->addLayout(listenLayout);

    QHBoxLayout* statusLayout = new QHBoxLayout();
    statusLabel = new QLabel(T("就绪"));
    
    statusLayout->addWidget(statusLabel);
    statusLayout->addStretch();
    leftLayout->addLayout(statusLayout);
    
    leftLayout->addStretch();

    connect(modeTcpBtn, &QRadioButton::toggled, this, [this](bool checked){ if(checked) onModeChanged(0); });
    connect(modeBtBtn, &QRadioButton::toggled, this, [this](bool checked){ if(checked) onModeChanged(1); });
    
    connect(startBtn, &QPushButton::clicked, this, &ControlWindow::onStart);
    connect(btListWidget, &QListWidget::itemClicked, this, &ControlWindow::onBtItemClicked);

    QClipboard *clip = QApplication::clipboard();
    connect(clip, &QClipboard::dataChanged, this,[=](){
        if (g_IgnoreClipUpdate) return;
        
        MDC_LOG_DEBUG(LogTag::UI, "Clipboard changed");
        const QMimeData* mime = clip->mimeData();
        if (mime->hasUrls()) {
            MDC_LOG_DEBUG(LogTag::UI, "Mime has URLs");
            std::vector<std::string> paths;
            for (const QUrl& url : mime->urls()) {
                if (url.isLocalFile()) {
                    std::string p = url.toLocalFile().toUtf8().constData();
                    paths.push_back(p);
                    MDC_LOG_DEBUG(LogTag::UI, "Local file path length: %zu", p.length());
                }
            }
            if (!paths.empty()) {
                if (g_ClientSock == INVALID_SOCKET_HANDLE) { 
                     MDC_LOG_INFO(LogTag::UI, "Sending %d files to MasterSendFileClipboard", (int)paths.size());
                     RunInQThread([paths]() { MasterSendFileClipboard(paths); });
                } else { 
                     MDC_LOG_INFO(LogTag::UI, "Sending %d files to SlaveSendFileClipboard", (int)paths.size());
                     RunInQThread([paths]() { SlaveSendFileClipboard(paths); });
                }
                return; 
            }
        }
        
        if (mime->hasText()) {
            std::string currentText = clip->text().toUtf8().toStdString();
            std::lock_guard<std::mutex> lock(g_SlaveListLock);
            if (!g_SlaveList.empty()) {
                if (!currentText.empty() && currentText != g_LastClipText) {
                    g_LastClipText = currentText;
                    MasterSendClipboard(currentText);
                }
            }
        }
    });
    
    g_MainObject = this;

    connect(listenTcpCb, &QCheckBox::checkStateChanged, this, &ControlWindow::startListening);
    connect(listenBtCb, &QCheckBox::checkStateChanged, this, &ControlWindow::startListening);
    connect(listenPortEdit, &QLineEdit::editingFinished, this, &ControlWindow::startListening);

    onModeChanged(0);
    startListening();
    
    m_connectWindow->show();
}

void ControlWindow::updateLanguageUI() {
    if (m_connectWindow) m_connectWindow->setWindowTitle(T("设备连接"));
    if (m_lblConnectAsMaster) m_lblConnectAsMaster->setText(T("作为主控端连接"));
    if (modeTcpBtn) modeTcpBtn->setText(T("网络"));
    if (modeBtBtn) modeBtBtn->setText(T("蓝牙"));
    if (m_lblIp) m_lblIp->setText(T("IP:"));
    if (m_lblPort) m_lblPort->setText(T("端口:"));
    if (m_lblListenAsMaster) m_lblListenAsMaster->setText(T("监听其它主控端连接"));
    if (listenBtCb) listenBtCb->setText(T("蓝牙"));
    if (startBtn) startBtn->setText(modeBtBtn->isChecked() ? T("扫描") : T("连接"));
    
    if (statusLabel->text() == "Ready" || statusLabel->text() == "就绪") {
        statusLabel->setText(T("就绪"));
    } else if (statusLabel->text() == "Listening for connections" || statusLabel->text() == "正在监听连接") {
        statusLabel->setText(T("正在监听连接"));
    }
    
    for (auto const& [tid, w] : m_transferWidgets) {
        if (w) w->updateTheme();
    }
    
    refreshBtList();
    if (mapWidget) mapWidget->update();
}

void ControlWindow::updateTrayMenu() {
    m_trayMenu->clear();

    QAction* showAct = m_trayMenu->addAction(T("打开主界面"));
    connect(showAct, &QAction::triggered, this, &QWidget::showNormal);

    uint32_t currentLat = 0;
    if (g_ClientSock != INVALID_SOCKET_HANDLE) {
        currentLat = g_Latency.load();
    } else {
        int idx = g_ActiveSlaveIdx.load();
        std::lock_guard<std::mutex> lock(g_SlaveListLock);
        if (idx >= 0 && idx < (int)g_SlaveList.size()) {
            currentLat = g_SlaveList[idx]->latency.load();
        } else if (!g_SlaveList.empty()) {
            currentLat = g_SlaveList[0]->latency.load();
        }
    }
    QAction* latAct = m_trayMenu->addAction(T("延迟: %1 ms").arg(currentLat));
    latAct->setDisabled(true);

    QMenu* devMenu = m_trayMenu->addMenu(T("已连接设备"));
    if (g_ClientSock != INVALID_SOCKET_HANDLE) {
        QAction* dAct = devMenu->addAction(T("主控端"));
        dAct->setDisabled(true);
    } else {
        std::lock_guard<std::mutex> lock(g_SlaveListLock);
        int count = 0;
        for (auto& s : g_SlaveList) {
            if (s->connected) {
                QAction* dAct = devMenu->addAction(QString::fromStdString(s->name));
                dAct->setDisabled(true);
                count++;
            }
        }
        if (count == 0) {
            QAction* dAct = devMenu->addAction(T("无设备"));
            dAct->setDisabled(true);
        }
    }

    m_trayMenu->addSeparator();

    QAction* stopAct = m_trayMenu->addAction(T("断开连接"));
    connect(stopAct, &QAction::triggered, this, &ControlWindow::onStop);

    QAction* exitAct = m_trayMenu->addAction(T("退出"));
    connect(exitAct, &QAction::triggered, qApp, &QApplication::quit);
}

void ControlWindow::closeEvent(QCloseEvent *event) {
    event->ignore();
    this->hide();
}

void ControlWindow::showConnectWindow() {
    if (m_connectWindow) {
        m_connectWindow->show();
        m_connectWindow->raise();
        m_connectWindow->activateWindow();
    }
}

static void setupHotkeyRow(QGridLayout* layout, int row, const QString& label, int& modRef, int& vkRef) {
    layout->addWidget(new QLabel(label), row, 0);

    QComboBox* modCombo = new QComboBox();
    modCombo->addItem(T("无"), 0);
    modCombo->addItem("Ctrl", 1);
    modCombo->addItem("Alt", 2);
    modCombo->addItem("Shift", 3);
    modCombo->addItem("Ctrl+Alt", 4);
    modCombo->setCurrentIndex(modCombo->findData(modRef));
    layout->addWidget(modCombo, row, 1);

    QLineEdit* keyEdit = new QLineEdit();
    keyEdit->setReadOnly(true);
    keyEdit->setPlaceholderText(T("点击后按键设定"));
    keyEdit->setToolTip(T("点击此处然后按下想要绑定的按键(字母/数字等)，按Esc清除"));
    if (vkRef > 0) {
        char name[32] = {0};
        UINT scanCode = MapVirtualKeyA(vkRef, MAPVK_VK_TO_VSC);
        GetKeyNameTextA(scanCode << 16, name, 32);
        keyEdit->setText(name);
    }
    layout->addWidget(keyEdit, row, 2);

    int* pVk = new int(vkRef); 

    keyEdit->installEventFilter(new QObject()); 
}

class KeyCatcher : public QObject {
    QLineEdit* edit;
    int* pVk;
public:
    KeyCatcher(QLineEdit* e, int* v, QObject* p) : QObject(p), edit(e), pVk(v) {
        e->installEventFilter(this);
    }
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* ke = static_cast<QKeyEvent*>(event);
            int vk = ke->nativeVirtualKey();
            if (vk == VK_ESCAPE) {
                *pVk = 0;
                edit->setText("");
            } else if (vk != VK_CONTROL && vk != VK_SHIFT && vk != VK_MENU && vk != VK_LWIN && vk != VK_RWIN) {
                *pVk = vk;
                char name[32] = {0};
                UINT scanCode = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
                GetKeyNameTextA(scanCode << 16, name, 32);
                edit->setText(name);
            }
            return true;
        }
        return QObject::eventFilter(obj, event);
    }
};

void ControlWindow::openSettings() {
    QDialog dlg(this);
    dlg.setWindowTitle(T("设置"));
    dlg.setMinimumWidth(400);
    QVBoxLayout* mainLayout = new QVBoxLayout(&dlg);
    
    QGroupBox* genGroup = new QGroupBox(T("常规设置"));
    QVBoxLayout* genLayout = new QVBoxLayout(genGroup);
    
    QHBoxLayout* langL = new QHBoxLayout();
    langL->addWidget(new QLabel(T("语言")));
    QComboBox* langCb = new QComboBox();
    langCb->addItem(T("自动"), 0);
    langCb->addItem(T("中文"), 1);
    langCb->addItem("English", 2);
    langCb->setCurrentIndex(langCb->findData(g_Language));
    langL->addWidget(langCb);
    genLayout->addLayout(langL);

    QHBoxLayout* themeL = new QHBoxLayout();
    themeL->addWidget(new QLabel(T("主题模式")));
    QComboBox* themeCb = new QComboBox();
    themeCb->addItem(T("跟随系统"), 0);
    themeCb->addItem(T("浅色"), 1);
    themeCb->addItem(T("深色"), 2);
    themeCb->setCurrentIndex(themeCb->findData(g_ThemeMode));
    themeL->addWidget(themeCb);
    genLayout->addLayout(themeL);

    mainLayout->addWidget(genGroup);

    QGroupBox* hkGroup = new QGroupBox(T("快捷键"));
    QGridLayout* hkLayout = new QGridLayout(hkGroup);
    
    int t_mod = g_HkToggleMod, t_vk = g_HkToggleVk;
    int l_mod = g_HkLockMod, l_vk = g_HkLockVk;
    int d_mod = g_HkDisconnMod, d_vk = g_HkDisconnVk;
    int e_mod = g_HkExitMod, e_vk = g_HkExitVk;

    hkLayout->addWidget(new QLabel(T("动作")), 0, 0);
    hkLayout->addWidget(new QLabel(T("修饰键")), 0, 1);
    hkLayout->addWidget(new QLabel(T("主键")), 0, 2);

    auto addRow = [&](int row, const QString& name, int& m, int& v) {
        hkLayout->addWidget(new QLabel(name), row, 0);
        QComboBox* cb = new QComboBox();
        cb->addItem(T("无"), 0); cb->addItem("Ctrl", 1); cb->addItem("Alt", 2); cb->addItem("Shift", 3); cb->addItem("Ctrl+Alt", 4);
        cb->setCurrentIndex(cb->findData(m));
        hkLayout->addWidget(cb, row, 1);
        
        QLineEdit* le = new QLineEdit();
        le->setReadOnly(true);
        le->setPlaceholderText(T("点击后按键设定"));
        le->setToolTip(T("点击此处然后按下想要绑定的按键(字母/数字等)，按Esc清除"));
        if (v > 0) {
            char kn[32] = {0};
            GetKeyNameTextA(MapVirtualKeyA(v, MAPVK_VK_TO_VSC) << 16, kn, 32);
            le->setText(kn);
        }
        new KeyCatcher(le, &v, &dlg);
        hkLayout->addWidget(le, row, 2);

        QObject::connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), [&m, cb]() {
            m = cb->currentData().toInt();
        });
    };

    addRow(1, T("显示/隐藏主界面"), t_mod, t_vk);
    addRow(2, T("锁定/解锁设备"), l_mod, l_vk);
    addRow(3, T("本机断开连接"), d_mod, d_vk);
    addRow(4, T("终止程序"), e_mod, e_vk);
    mainLayout->addWidget(hkGroup);

    QGroupBox* fileGroup = new QGroupBox(T("传输与连接"));
    QVBoxLayout* fileLayout = new QVBoxLayout(fileGroup);
    
    QHBoxLayout* pathL = new QHBoxLayout();
    pathL->addWidget(new QLabel(T("备用保存路径:")));
    QLineEdit* pathEdit = new QLineEdit(QString::fromStdString(g_FallbackTransferPath));
    pathL->addWidget(pathEdit);
    QPushButton* browseBtn = new QPushButton(T("浏览"));
    pathL->addWidget(browseBtn);
    QObject::connect(browseBtn, &QPushButton::clicked, [&](){
        QString dir = QFileDialog::getExistingDirectory(&dlg, T("选择备用路径"), pathEdit->text());
        if (!dir.isEmpty()) {
            dir.replace("/", "\\");
            pathEdit->setText(dir);
        }
    });
    fileLayout->addLayout(pathL);
    
    QCheckBox* rememberPosCb = new QCheckBox(T("记住上次连接的被控端位置"));
    rememberPosCb->setChecked(g_RememberPos);
    fileLayout->addWidget(rememberPosCb);
    
    mainLayout->addWidget(fileGroup);

    QGroupBox* debugGroup = new QGroupBox(T("调试与日志"));
    QVBoxLayout* debugLayout = new QVBoxLayout(debugGroup);
    
    QHBoxLayout* logL = new QHBoxLayout();
    QCheckBox* logCb = new QCheckBox(T("保存日志"));
    logCb->setChecked(g_LogToFile);
    logL->addWidget(logCb);
    QPushButton* openLogBtn = new QPushButton(T("浏览"));
    logL->addWidget(openLogBtn);
    debugLayout->addLayout(logL);

    QHBoxLayout* logLevelL = new QHBoxLayout();
    logLevelL->addWidget(new QLabel(T("日志级别")));
    QComboBox* logLevelCb = new QComboBox();
    logLevelCb->addItem("TRACE", 0);
    logLevelCb->addItem("DEBUG", 1);
    logLevelCb->addItem("INFO", 2);
    logLevelCb->addItem("WARN", 3);
    logLevelCb->addItem("ERROR", 4);
    logLevelCb->setCurrentIndex(logLevelCb->findData(g_LogLevel));
    logLevelL->addWidget(logLevelCb);
    debugLayout->addLayout(logLevelL);

    QObject::connect(openLogBtn, &QPushButton::clicked, [](){
        SystemUtils::OpenLogDirectory();
    });

    mainLayout->addWidget(debugGroup);
    
    QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    mainLayout->addWidget(box);
    QObject::connect(box, &QDialogButtonBox::accepted, [&](){
        MDC_LOG_INFO(LogTag::UI, "Settings saved language: %d logLevel: %d", langCb->currentData().toInt(), logLevelCb->currentData().toInt());
        int oldLang = g_Language;
        int oldTheme = g_ThemeMode;
        g_Language = langCb->currentData().toInt();
        g_ThemeMode = themeCb->currentData().toInt();

        g_HkToggleMod = t_mod; g_HkToggleVk = t_vk;
        g_HkLockMod = l_mod; g_HkLockVk = l_vk;
        g_HkDisconnMod = d_mod; g_HkDisconnVk = d_vk;
        g_HkExitMod = e_mod; g_HkExitVk = e_vk;
        g_FallbackTransferPath = pathEdit->text().toStdString();
        g_RememberPos = rememberPosCb->isChecked();
        
        g_LogToFile = logCb->isChecked();
        g_LogLevel = logLevelCb->currentData().toInt();

        QSettings s("MDControl", "Settings");
        s.setValue("HkToggle_Mod", g_HkToggleMod); s.setValue("HkToggle_VK", g_HkToggleVk);
        s.setValue("HkLock_Mod", g_HkLockMod); s.setValue("HkLock_VK", g_HkLockVk);
        s.setValue("HkDisconn_Mod", g_HkDisconnMod); s.setValue("HkDisconn_VK", g_HkDisconnVk);
        s.setValue("HkExit_Mod", g_HkExitMod); s.setValue("HkExit_VK", g_HkExitVk);
        s.setValue("FallbackPath", QString::fromStdString(g_FallbackTransferPath));
        s.setValue("LogToFile", g_LogToFile);
        s.setValue("LogLevel", g_LogLevel);
        s.setValue("RememberPos", g_RememberPos);
        s.setValue("Language", g_Language);
        s.setValue("ThemeMode", g_ThemeMode);

        SystemUtils::ApplyTheme(g_ThemeMode);
        
        if (oldLang != g_Language || oldTheme != g_ThemeMode) {
            updateLanguageUI();
        }
        
        dlg.accept();
    });
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    
    dlg.exec();
}

void ControlWindow::refreshBtList() {
    btListWidget->clear();
    
    QSettings settings("MDControl", "Devices");
    QMap<QString, QVariant> history = settings.value("History").toMap();
    QStringList pairedList = settings.value("PairedMACs").toStringList();
    
    struct BtItem {
        unsigned long long mac;
        QString name;
        bool isHistory;
        bool isPaired;
    };
    std::map<unsigned long long, BtItem> merged;
    
    QMapIterator<QString, QVariant> i(history);
    while (i.hasNext()) {
        i.next();
        unsigned long long mac = i.key().toULongLong();
        merged[mac] = {mac, i.value().toString(), true, pairedList.contains(i.key())};
    }
    
    for (auto const& [mac, name] : m_scannedBtDevices) {
        if (merged.find(mac) == merged.end()) {
            merged[mac] = {mac, name, false, pairedList.contains(QString::number(mac))};
        } else {
            if (name != "Unknown") merged[mac].name = name;
        }
    }
    
    std::vector<BtItem> cat3, cat2, cat1;
    for (auto const& [mac, item] : merged) {
        if (item.isHistory && item.isPaired) cat3.push_back(item);
        else if (item.isHistory) cat2.push_back(item);
        else cat1.push_back(item);
    }
    
    auto addToList = [&](const std::vector<BtItem>& list) {
        for (const auto& item : list) {
            char addrStr[32]; sprintf(addrStr, "%012llX", item.mac);
            QString display = QString("%1 (%2)").arg(item.name).arg(addrStr);
            
            QListWidgetItem* lwItem = new QListWidgetItem();
            lwItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(item.mac));
            lwItem->setSizeHint(QSize(0, 36)); 
            btListWidget->addItem(lwItem);

            QWidget* widget = new QWidget();
            QHBoxLayout* layout = new QHBoxLayout(widget);
            layout->setContentsMargins(5, 0, 5, 0);
            
            QLabel* iconLabel = new QLabel();
            iconLabel->setPixmap(IconDrawer::getBtDeviceIcon(item.isHistory, item.isPaired).pixmap(QSize(48, 24)));
            
            QLabel* textLabel = new QLabel(display);
            textLabel->setStyleSheet("background: transparent;");
            
            QPushButton* delBtn = new QPushButton();
            delBtn->setIcon(IconDrawer::getDeleteIcon());
            delBtn->setFixedSize(28, 28);
            delBtn->setFlat(true);
            delBtn->setCursor(Qt::PointingHandCursor);
            delBtn->setToolTip(T("清除历史与配稳记录"));
            delBtn->setStyleSheet("background: transparent;");
            
            connect(delBtn, &QPushButton::clicked, this, [this, item]() {
                QSettings settings("MDControl", "Devices");
                QMap<QString, QVariant> hist = settings.value("History").toMap();
                hist.remove(QString::number(item.mac));
                settings.setValue("History", hist);
                
                QStringList paired = settings.value("PairedMACs").toStringList();
                paired.removeAll(QString::number(item.mac));
                settings.setValue("PairedMACs", paired);

                QSettings authSettings("MDControl", "Auth");
                char addrStr[32]; sprintf(addrStr, "%012llX", item.mac);
                QString peerId = QString::fromStdString(std::string(addrStr));
                QMap<QString, QVariant> peerToKey = authSettings.value("PeerToKey").toMap();
                if (peerToKey.contains(peerId)) {
                    QString keyToRemove = peerToKey[peerId].toString();
                    QStringList trustedKeys = authSettings.value("TrustedKeys").toStringList();
                    trustedKeys.removeAll(keyToRemove);
                    authSettings.setValue("TrustedKeys", trustedKeys);
                    peerToKey.remove(peerId);
                    authSettings.setValue("PeerToKey", peerToKey);
                }

                m_scannedBtDevices.erase(item.mac);
                
                refreshBtList();
            });
            
            layout->addWidget(iconLabel, 0, Qt::AlignLeft);
            layout->addWidget(textLabel, 1, Qt::AlignLeft);
            layout->addWidget(delBtn, 0, Qt::AlignRight);

            if (!item.isHistory && !item.isPaired) {
                delBtn->setVisible(false);
            }
            
            btListWidget->setItemWidget(lwItem, widget);
        }
    };
    
    addToList(cat3);
    addToList(cat2);
    addToList(cat1);
}

void ControlWindow::onModeChanged(int index) {
    bool isBT = (index == 1);
    tcpConfigWidget->setVisible(!isBT); 
    btListWidget->setVisible(isBT); 
    startBtn->setText(isBT ? T("扫描") : T("连接"));
    
    if (isBT) {
        refreshBtList();
    }
}

void ControlWindow::stopListening() {
    if (g_TcpListenSock != INVALID_SOCKET_HANDLE) {
        NetUtils::CloseSocket(g_TcpListenSock);
        g_TcpListenSock = INVALID_SOCKET_HANDLE;
    }
    if (g_Context->BluetoothMgr) {
        g_Context->BluetoothMgr->StopListen();
    }
}

void ControlWindow::startListening() {
    stopListening(); 

    bool hasMasterConn = false;
    {
        std::lock_guard<std::mutex> lock(g_SlaveListLock);
        for(auto& ctx: g_SlaveList) {
            if(ctx->connected) hasMasterConn = true;
        }
    }
    if (hasMasterConn || g_ClientSock != INVALID_SOCKET_HANDLE) {
        return;
    }

    if (listenTcpCb->isChecked()) {
        int port = listenPortEdit->text().toInt();
        RunInQThread([port]() {
            SocketHandle tcpListen = (SocketHandle)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            g_TcpListenSock = tcpListen;
            SOCKADDR_IN sa = { 0 }; sa.sin_family = AF_INET; sa.sin_addr.s_addr = INADDR_ANY; sa.sin_port = htons(port);
            bind((SOCKET)tcpListen, (SOCKADDR*)&sa, sizeof(sa));
            listen((SOCKET)tcpListen, 2); 
            
            while (g_Running && g_TcpListenSock == tcpListen) {
                MDC_LOG_INFO(LogTag::NET, "Waiting for master TCP port %d", port);
                SOCKET clientControl = accept((SOCKET)tcpListen, NULL, NULL);
                if (clientControl == INVALID_SOCKET || g_TcpListenSock != tcpListen) break;
                if (!g_Running) break;
                
                MDC_LOG_INFO(LogTag::NET, "TCP control connected waiting for file channel");
                SOCKET clientFile = accept((SOCKET)tcpListen, NULL, NULL);
                if (clientFile == INVALID_SOCKET) {
                    closesocket(clientControl);
                    break;
                }
                
                MDC_LOG_INFO(LogTag::NET, "TCP file connected");

                if (!AuthSlave((SocketHandle)clientControl)) {
                    MDC_LOG_WARN(LogTag::NET, "TCP auth failed");
                    closesocket(clientControl);
                    closesocket(clientFile);
                    continue;
                }

                QMetaObject::invokeMethod(g_MainObject,[](){
                    if (g_MainObject) ((ControlWindow*)g_MainObject)->stopListening();
                });

                char mode = 0; 
                recv(clientControl, &mode, 1, 0); 
                
                MDC_LOG_INFO(LogTag::NET, "Auth successful beginning handshake sequence");
                SocketHandle sock = (SocketHandle)clientControl;
                unsigned int netW = htonl(g_LocalW), netH = htonl(g_LocalH);
                char resBuf[8]; memcpy(resBuf, &netW, 4); memcpy(resBuf + 4, &netH, 4);
                send(sock, resBuf, 8, 0);

                char scaleBuf[9]; scaleBuf[0] = 28;
                double myScale = g_LocalScale;
                memcpy(scaleBuf + 1, &myScale, 8);
                send(sock, scaleBuf, 9, 0);

                std::vector<char> pkt25;
                pkt25.push_back(25);
                unsigned int plen = htonl((unsigned int)g_MySysProps.length());
                pkt25.insert(pkt25.end(), (char*)&plen, (char*)&plen + 4);
                pkt25.insert(pkt25.end(), g_MySysProps.begin(), g_MySysProps.end());
                send(sock, pkt25.data(), pkt25.size(), 0);
                
                char configBuf[25];
                int ret = recv(sock, configBuf, 25, 0);
                
                if (ret == 25 && configBuf[0] == 10) {
                    unsigned int mW, mH;
                    memcpy(&mW, configBuf + 1, 4);
                    memcpy(&mH, configBuf + 5, 4);
                    g_SlaveW = ntohl(mW); 
                    g_SlaveH = ntohl(mH); 
                    double lx, ly;
                    memcpy(&lx, configBuf + 9, 8);
                    memcpy(&ly, configBuf + 17, 8);
                    g_LogicalX = lx;
                    g_LogicalY = ly;
                }

                if (g_MainObject) {
                    QCoreApplication::postEvent(g_MainObject, new SlaveConnectedEvent((InterfaceSocketHandle)clientControl, (InterfaceSocketHandle)clientFile));
                } else {
                    closesocket(clientControl);
                    closesocket(clientFile);
                }
                break;
            }
            if (tcpListen != INVALID_SOCKET) {
                closesocket((SOCKET)tcpListen);
            }
        });
    }

    if (listenBtCb->isChecked()) {
        RunInQThread([]() {
            while (g_Running) {
                if (g_Context->BluetoothMgr) {
                    auto clients = g_Context->BluetoothMgr->Listen(4, 5);
                    if (clients.size() >= 1 && g_Running) {
                        MDC_LOG_INFO(LogTag::NET, "Bluetooth control connected single or dual channel");
                        InterfaceSocketHandle sCtrl = clients[0];
                        InterfaceSocketHandle sFile = clients.size() >= 2 ? clients[1] : (InterfaceSocketHandle)INVALID_SOCKET_HANDLE;
                        
                        if (!AuthSlave((SocketHandle)sCtrl)) {
                            MDC_LOG_WARN(LogTag::NET, "BTH auth failed");
                            NetUtils::CloseSocket((SocketHandle)sCtrl);
                            if (sFile != (InterfaceSocketHandle)INVALID_SOCKET_HANDLE) NetUtils::CloseSocket((SocketHandle)sFile);
                            continue;
                        }

                        QMetaObject::invokeMethod(g_MainObject,[](){
                            if (g_MainObject) ((ControlWindow*)g_MainObject)->stopListening();
                        });

                        MDC_LOG_INFO(LogTag::NET, "Auth successful beginning handshake sequence");
                        SocketHandle sock = (SocketHandle)sCtrl;
                        unsigned int netW = htonl(g_LocalW), netH = htonl(g_LocalH);
                        char resBuf[8]; memcpy(resBuf, &netW, 4); memcpy(resBuf + 4, &netH, 4);
                        send(sock, resBuf, 8, 0);

                        char scaleBuf[9]; scaleBuf[0] = 28;
                        double myScale = g_LocalScale;
                        memcpy(scaleBuf + 1, &myScale, 8);
                        send(sock, scaleBuf, 9, 0);

                        std::vector<char> pkt25;
                        pkt25.push_back(25);
                        unsigned int plen = htonl((unsigned int)g_MySysProps.length());
                        pkt25.insert(pkt25.end(), (char*)&plen, (char*)&plen + 4);
                        pkt25.insert(pkt25.end(), g_MySysProps.begin(), g_MySysProps.end());
                        send(sock, pkt25.data(), pkt25.size(), 0);
                        
                        char configBuf[25];
                        int ret = recv(sock, configBuf, 25, 0);
                        
                        if (ret == 25 && configBuf[0] == 10) {
                            unsigned int mW, mH;
                            memcpy(&mW, configBuf + 1, 4);
                            memcpy(&mH, configBuf + 5, 4);
                            g_SlaveW = ntohl(mW); 
                            g_SlaveH = ntohl(mH); 
                            double lx, ly;
                            memcpy(&lx, configBuf + 9, 8);
                            memcpy(&ly, configBuf + 17, 8);
                            g_LogicalX = lx;
                            g_LogicalY = ly;
                        }

                        if (g_MainObject) {
                            QCoreApplication::postEvent(g_MainObject, new SlaveConnectedEvent(sCtrl, sFile));
                        } else {
                            NetUtils::CloseSocket((SocketHandle)sCtrl);
                            if (sFile != (InterfaceSocketHandle)INVALID_SOCKET_HANDLE) NetUtils::CloseSocket((SocketHandle)sFile);
                        }
                        break;
                    } else {
                        Sleep(100); 
                        if (!g_Running) break;
                        continue; 
                    }
                } else {
                    break;
                }
            }
        });
    }
    statusLabel->setText(T("正在监听连接"));
}

void StartMasterWithSockets(SocketHandle sockControl, SocketHandle sockFile, std::string name, std::string addr, bool isBT) {
    char res[8]; 
    MDC_LOG_DEBUG(LogTag::NET, "Waiting for 8 bytes config from slave");
    if (!NetUtils::RecvAll(sockControl, res, 8)) {
        MDC_LOG_WARN(LogTag::NET, "Failed to receive 8 bytes connection dropped");
        NetUtils::CloseSocket(sockControl);
        if(sockFile != INVALID_SOCKET_HANDLE) NetUtils::CloseSocket(sockFile);
        return;
    }
    MDC_LOG_DEBUG(LogTag::NET, "Successfully received 8 bytes config");
    unsigned int rW, rH; memcpy(&rW, res, 4); memcpy(&rH, res + 4, 4);
    
    char scaleBuf[9];
    double sScale = 1.0;
    if (NetUtils::RecvAll(sockControl, scaleBuf, 9) && scaleBuf[0] == 28) {
        memcpy(&sScale, scaleBuf + 1, 8);
    } else {
        NetUtils::CloseSocket(sockControl);
        if(sockFile != INVALID_SOCKET_HANDLE) NetUtils::CloseSocket(sockFile);
        return;
    }

    char pkt25Hdr[5];
    std::string props = "";
    if (NetUtils::RecvAll(sockControl, pkt25Hdr, 5) && pkt25Hdr[0] == 25) {
        unsigned int pLen; memcpy(&pLen, pkt25Hdr + 1, 4);
        int propsLen = ntohl(pLen);
        if (propsLen > 0 && propsLen < 1024 * 1024) { 
            std::vector<char> pData(propsLen);
            if (NetUtils::RecvAll(sockControl, pData.data(), propsLen)) {
                props = std::string(pData.begin(), pData.end());
            } else {
                NetUtils::CloseSocket(sockControl);
                if(sockFile != INVALID_SOCKET_HANDLE) NetUtils::CloseSocket(sockFile);
                return;
            }
        }
    } else {
        NetUtils::CloseSocket(sockControl);
        if(sockFile != INVALID_SOCKET_HANDLE) NetUtils::CloseSocket(sockFile);
        return;
    }

    std::shared_ptr<SlaveCtx> ctx;
    bool reused = false;
    {
        std::lock_guard<std::mutex> lock(g_SlaveListLock);
        for(auto& existing : g_SlaveList) {
            if (!existing->connected && existing->connectAddress == addr && existing->isBluetooth == isBT) {
                ctx = existing;
                reused = true;
                break;
            }
        }
    }
    if (!ctx) ctx = std::make_shared<SlaveCtx>();

    ctx->sock = sockControl;
    ctx->fileSock = sockFile;
    if (isBT) ctx->btFileSock = sockFile;
    ctx->width = ntohl(rW);
    ctx->height = ntohl(rH);
    ctx->scale = sScale;
    ctx->sysProps = props;
    ctx->name = name;
    ctx->ratioX = (double)ctx->width / g_LocalW;
    ctx->ratioY = (double)ctx->height / g_LocalH;
    ctx->connected = true;
    ctx->connectAddress = addr;
    ctx->isBluetooth = isBT;
    ctx->tcpFileFailed = false; 
    ctx->lastSentTopo = "";
    
    {
        std::lock_guard<std::mutex> lock(g_SlaveListLock);
        
        if (!reused) {
            bool posLoaded = false;
            if (g_RememberPos) {
                QSettings settings("MDControl", "DevicePositions");
                QString key = QString::fromStdString(addr) + (isBT ? "_BT" : "_TCP");
                if (settings.contains(key + "_X") && settings.contains(key + "_Y")) {
                    ctx->logicalX = settings.value(key + "_X").toDouble();
                    ctx->logicalY = settings.value(key + "_Y").toDouble();
                    posLoaded = true;
                }
            }

            if (!posLoaded) {
                ctx->logicalX = g_LocalW / g_LocalScale + 50.0;
                ctx->logicalY = 0.0;
            }

            auto isOverlap = [&](double testX, double testY) {
                double tw = ctx->width / ctx->scale;
                double th = ctx->height / ctx->scale;
                QRectF testR(testX, testY, tw, th);
                QRectF shrinkTestR = testR.adjusted(1, 1, -1, -1);
                
                QRectF masterR(0, 0, g_LocalW / g_LocalScale, g_LocalH / g_LocalScale);
                if (shrinkTestR.intersects(masterR)) return true;
                
                for (const auto& other : g_SlaveList) {
                    if (other->connected && other != ctx) {
                        double ow = other->width / other->scale;
                        double oh = other->height / other->scale;
                        QRectF otherR(other->logicalX, other->logicalY, ow, oh);
                        if (shrinkTestR.intersects(otherR)) return true;
                    }
                }
                return false;
            };

            while (isOverlap(ctx->logicalX, ctx->logicalY)) {
                ctx->logicalX += 50.0;
                ctx->logicalY += 50.0;
            }

            g_SlaveList.push_back(ctx);
        }
    }

    char configBuf[25];
    configBuf[0] = 10;
    unsigned int mW = htonl(g_LocalW);
    unsigned int mH = htonl(g_LocalH);
    memcpy(configBuf + 1, &mW, 4);
    memcpy(configBuf + 5, &mH, 4);
    double lX = ctx->logicalX;
    double lY = ctx->logicalY;
    memcpy(configBuf + 9, &lX, 8);
    memcpy(configBuf + 17, &lY, 8);
    
    MDC_LOG_DEBUG(LogTag::NET, "Sending 25 bytes pos config to slave");
    send(sockControl, configBuf, 25, 0);

    std::vector<char> pkt25;
    pkt25.push_back(25);
    unsigned int plen = htonl((unsigned int)g_MySysProps.length());
    pkt25.insert(pkt25.end(), (char*)&plen, (char*)&plen + 4);
    pkt25.insert(pkt25.end(), g_MySysProps.begin(), g_MySysProps.end());
    MDC_LOG_DEBUG(LogTag::NET, "Sending pkt25 sys properties to slave");
    send(sockControl, pkt25.data(), pkt25.size(), 0);
    MDC_LOG_INFO(LogTag::NET, "Master handshake logic executed");
}

void ControlWindow::onBtItemClicked(QListWidgetItem* item) {
    if (g_Context->BluetoothMgr && !g_Context->BluetoothMgr->IsAvailable()) {
        QMessageBox::warning(this, T("提示"), T("蓝牙未打开或不可用"));
        return;
    }

    g_Running = true;
    stopListening();
    
    if (g_Context->BluetoothMgr) g_Context->BluetoothMgr->StopScan();
    
    unsigned long long addr = item->data(Qt::UserRole).toULongLong();
    
    QString name;
    QWidget* widget = btListWidget->itemWidget(item);
    if (widget) {
        auto labels = widget->findChildren<QLabel*>();
        for (auto* lbl : labels) {
            if (!lbl->text().isEmpty()) {
                name = lbl->text().split(" (").first();
                break;
            }
        }
    } else {
        name = item->text().split(" (").first();
    }
    
    char addrStr[32]; sprintf(addrStr, "%012llX", addr);
    std::string sAddr(addrStr);
    std::string stdName = name.toStdString();

    QSettings settings("MDControl", "Devices");
    QMap<QString, QVariant> history = settings.value("History").toMap();
    history[QString::number(addr)] = name;
    settings.setValue("History", history);
    
    refreshBtList();
    
    MDC_LOG_INFO(LogTag::BTH, "Initiating connection to %012llX on port 4", addr);
    statusLabel->setText(T("正在连接"));
    startBtn->setEnabled(false);
    btListWidget->setEnabled(false);

    RunInQThread([this, addr, stdName, sAddr]() {
        Sleep(300);
        
        if (g_Context->BluetoothMgr) {
            InterfaceSocketHandle sControl = g_Context->BluetoothMgr->Connect(addr, 4);
            if ((SocketHandle)sControl != INVALID_SOCKET_HANDLE) {
                 MDC_LOG_INFO(LogTag::BTH, "Control connection established. Initiating file connection");
                 InterfaceSocketHandle sFile = g_Context->BluetoothMgr->Connect(addr, 5);

                 if (!AuthMaster((SocketHandle)sControl, stdName)) {
                     MDC_LOG_WARN(LogTag::BTH, "BTH AuthMaster failed");
                     NetUtils::CloseSocket((SocketHandle)sControl);
                     if ((SocketHandle)sFile != INVALID_SOCKET_HANDLE) NetUtils::CloseSocket((SocketHandle)sFile);
                     
                     QMetaObject::invokeMethod(this, [this]() {
                         QMessageBox::critical(this, T("配对失败"), T("验证未通过或已取消。"));
                         bool hasConn = false;
                         { std::lock_guard<std::mutex> lock(g_SlaveListLock); for (auto& c : g_SlaveList) if (c->connected) hasConn = true; }
                         if (hasConn) statusLabel->setText(T("连接失败，保持现有连接"));
                         else { statusLabel->setText(T("就绪")); startListening(); }
                         startBtn->setEnabled(true); btListWidget->setEnabled(true); ipEdit->setEnabled(true); portEdit->setEnabled(true); modeTcpBtn->setEnabled(true); modeBtBtn->setEnabled(true);
                     });
                     return;
                 }
                 
                 QMetaObject::invokeMethod(this, [this]() {
                     refreshBtList();
                 });
                 
                 StartMasterWithSockets((SocketHandle)sControl, (SocketHandle)sFile, stdName, sAddr, true);
                 
                 std::shared_ptr<SlaveCtx> ctx;
                 {
                     std::lock_guard<std::mutex> lock(g_SlaveListLock);
                     for (auto& c : g_SlaveList) {
                         if (c->connectAddress == sAddr && c->isBluetooth == true) {
                             ctx = c; break;
                         }
                     }
                 }

                 if (ctx) {
                     ctx->tcpFileFailed = true;
                 }
                 
                 char handshakePkt = 20; 
                 send((SocketHandle)sControl, &handshakePkt, 1, 0);
                     
                 QMetaObject::invokeMethod(this,[this, stdName, ctx]() {
                     statusLabel->setText(T("已连接: %1").arg(QString::fromStdString(stdName)));
                     InitOverlay();
                     
                     bool isFirst = false;
                     { std::lock_guard<std::mutex> lock(g_SlaveListLock); int cnt=0; for(auto& c:g_SlaveList) if(c->connected) cnt++; if(cnt==1) isFirst = true; }
                     if (isFirst) {
                         RunInQThread(SenderThread);
                         StartClipboardSenderThread();
                     }
                         
                     if (ctx) {
                         StartReceiverThread(ctx);
                         StartLatencyThread(ctx);
                     }

                     startBtn->setEnabled(true); btListWidget->setEnabled(true); ipEdit->setEnabled(true); portEdit->setEnabled(true); modeTcpBtn->setEnabled(true); modeBtBtn->setEnabled(true);
                 });
            } else {
                QMetaObject::invokeMethod(this,[this]() {
                    QMessageBox::critical(this, T("错误"), T("TCP控制通道连接失败"));
                    bool hasConn = false;
                    { std::lock_guard<std::mutex> lock(g_SlaveListLock); for (auto& c : g_SlaveList) if (c->connected) hasConn = true; }
                    if (hasConn) statusLabel->setText(T("连接失败，保持现有连接"));
                    else { statusLabel->setText(T("就绪")); startListening(); }
                    startBtn->setEnabled(true); btListWidget->setEnabled(true); ipEdit->setEnabled(true); portEdit->setEnabled(true); modeTcpBtn->setEnabled(true); modeBtBtn->setEnabled(true);
                });
            }
        }
    });
}

void ControlWindow::onStart() {
    bool isBT = modeBtBtn->isChecked();
    MDC_LOG_INFO(LogTag::UI, "User clicked start connection isBT: %d", isBT);

    if (isBT && g_Context->BluetoothMgr && !g_Context->BluetoothMgr->IsAvailable()) {
        QMessageBox::warning(this, T("提示"), T("蓝牙未打开或不可用"));
        return;
    }
    
    if (!isBT && !SystemUtils::HasNetworkConnectivity()) {
        QMessageBox::warning(this, T("提示"), T("网络未连接或不可用"));
        return;
    }

    g_Running = true;
    g_IsBluetoothConn = isBT;
    
    stopListening();
    
    if (isBT) {
        statusLabel->setText(T("正在扫描"));
        btListWidget->setEnabled(true);
        startBtn->setEnabled(false);
        
        if (g_Context->BluetoothMgr) {
            g_Context->BluetoothMgr->StartScan([=](const BluetoothDevice& dev){
                    QMetaObject::invokeMethod(this, [=](){
                        bool needRefresh = false;
                        if (m_scannedBtDevices.find(dev.address) == m_scannedBtDevices.end()) {
                            m_scannedBtDevices[dev.address] = QString::fromStdString(dev.name);
                            needRefresh = true;
                        } else if (m_scannedBtDevices[dev.address] == "Unknown" && dev.name != "Unknown") {
                            m_scannedBtDevices[dev.address] = QString::fromStdString(dev.name);
                            needRefresh = true;
                        }
                        if (needRefresh) {
                            refreshBtList();
                        }
                    });
                },[=](const std::string& msg){
                    QMetaObject::invokeMethod(this,[=](){ 
                        statusLabel->setText(QString::fromStdString(msg)); 
                        startBtn->setEnabled(true);
                    });
                }
            );
        }

    } else { 
        g_TargetIP = ipEdit->text().toStdString();
        g_TargetPort = portEdit->text().toInt();

        statusLabel->setText(T("正在连接"));
        startBtn->setEnabled(false);
        ipEdit->setEnabled(false);
        portEdit->setEnabled(false);
        modeTcpBtn->setEnabled(false);
        modeBtBtn->setEnabled(false);

        std::string ip = g_TargetIP;
        int port = g_TargetPort;

        RunInQThread([this, ip, port]() {
            SOCKET sockControl = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            SOCKADDR_IN sa = { 0 }; sa.sin_family = AF_INET; sa.sin_addr.s_addr = inet_addr(ip.c_str()); sa.sin_port = htons(port);
            if (::connect(sockControl, (SOCKADDR*)&sa, sizeof(sa)) == SOCKET_ERROR) {
                QMetaObject::invokeMethod(this,[this]() {
                    QMessageBox::critical(this, T("错误"), T("TCP控制通道连接失败"));
                    bool hasConn = false;
                    { std::lock_guard<std::mutex> lock(g_SlaveListLock); for (auto& c : g_SlaveList) if (c->connected) hasConn = true; }
                    if (hasConn) statusLabel->setText(T("连接失败，保持现有连接"));
                    else { statusLabel->setText(T("就绪")); startListening(); }
                    startBtn->setEnabled(true); btListWidget->setEnabled(true); ipEdit->setEnabled(true); portEdit->setEnabled(true); modeTcpBtn->setEnabled(true); modeBtBtn->setEnabled(true);
                });
                return;
            }
            NetUtils::SetNoDelay((SocketHandle)sockControl);
            
            SOCKET sockFile = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (::connect(sockFile, (SOCKADDR*)&sa, sizeof(sa)) == SOCKET_ERROR) {
                closesocket(sockControl);
                QMetaObject::invokeMethod(this,[this]() {
                    QMessageBox::critical(this, T("错误"), T("TCP文件通道连接失败"));
                    bool hasConn = false;
                    { std::lock_guard<std::mutex> lock(g_SlaveListLock); for (auto& c : g_SlaveList) if (c->connected) hasConn = true; }
                    if (hasConn) statusLabel->setText(T("连接失败，保持现有连接"));
                    else { statusLabel->setText(T("就绪")); startListening(); }
                    startBtn->setEnabled(true); btListWidget->setEnabled(true); ipEdit->setEnabled(true); portEdit->setEnabled(true); modeTcpBtn->setEnabled(true); modeBtBtn->setEnabled(true);
                });
                return;
            }

            if (!AuthMaster((SocketHandle)sockControl, ip)) {
                closesocket(sockControl);
                closesocket(sockFile);
                QMetaObject::invokeMethod(this,[this]() {
                    QMessageBox::critical(this, T("配对失败"), T("验证未通过或已取消。"));
                    bool hasConn = false;
                    { std::lock_guard<std::mutex> lock(g_SlaveListLock); for (auto& c : g_SlaveList) if (c->connected) hasConn = true; }
                    if (hasConn) statusLabel->setText(T("连接失败，保持现有连接"));
                    else { statusLabel->setText(T("就绪")); startListening(); }
                    startBtn->setEnabled(true); btListWidget->setEnabled(true); ipEdit->setEnabled(true); portEdit->setEnabled(true); modeTcpBtn->setEnabled(true); modeBtBtn->setEnabled(true);
                });
                return;
            }

            char m = 1; send(sockControl, &m, 1, 0);
            
            StartMasterWithSockets((SocketHandle)sockControl, (SocketHandle)sockFile, ip, ip, false);
            
            std::shared_ptr<SlaveCtx> ctx;
            {
                std::lock_guard<std::mutex> lock(g_SlaveListLock);
                for (auto& c : g_SlaveList) {
                    if (c->connectAddress == ip && c->isBluetooth == false) {
                        ctx = c; break;
                    }
                }
            }
            
            QMetaObject::invokeMethod(this,[this, ip, ctx]() {
                statusLabel->setText(T("已连接: %1").arg(QString::fromStdString(ip)));
                InitOverlay();
                
                bool isFirst = false;
                { std::lock_guard<std::mutex> lock(g_SlaveListLock); int cnt=0; for(auto& c:g_SlaveList) if(c->connected) cnt++; if(cnt==1) isFirst = true; }
                if (isFirst) {
                    RunInQThread(SenderThread);
                    StartClipboardSenderThread();
                }
                
                if (ctx) {
                    StartReceiverThread(ctx);
                    StartLatencyThread(ctx);
                }

                startBtn->setEnabled(true); btListWidget->setEnabled(true); ipEdit->setEnabled(true); portEdit->setEnabled(true); modeTcpBtn->setEnabled(true); modeBtBtn->setEnabled(true);
            });
        });
    }
}

void ControlWindow::onStop() {
    MDC_LOG_INFO(LogTag::UI, "User clicked stop connection");
    g_Running = false;
    
    if (g_Context->BluetoothMgr) g_Context->BluetoothMgr->StopScan();
    
    {
        std::lock_guard<std::mutex> lock(g_SlaveListLock);
        for(auto& ctx : g_SlaveList) {
            if(ctx->sock != INVALID_SOCKET_HANDLE) {
                NetUtils::ShutdownSocket(ctx->sock);
                NetUtils::CloseSocket(ctx->sock);
            }
            if(ctx->fileSock != INVALID_SOCKET_HANDLE) {
                NetUtils::ShutdownSocket(ctx->fileSock);
                NetUtils::CloseSocket(ctx->fileSock);
            }
            if(ctx->btFileSock != INVALID_SOCKET_HANDLE && ctx->btFileSock != ctx->fileSock) {
                NetUtils::ShutdownSocket(ctx->btFileSock);
                NetUtils::CloseSocket(ctx->btFileSock);
            }
        }
        g_SlaveList.clear();
    }
    
    g_ActiveSlaveIdx = -1;
    g_IsRemote = false;
    g_Locked = false;
    
    if (g_ClientSock != INVALID_SOCKET_HANDLE) { 
        auto s1 = g_ClientSock;
        auto s2 = g_ClientFileSock;
        auto s3 = g_ClientBtFileSock;
        g_ClientSock = INVALID_SOCKET_HANDLE;
        g_ClientFileSock = INVALID_SOCKET_HANDLE;
        g_ClientBtFileSock = INVALID_SOCKET_HANDLE;
        
        RunInQThread([s1, s2, s3]() {
            if (s1 != INVALID_SOCKET_HANDLE) NetUtils::CloseSocket(s1);
            if (s2 != INVALID_SOCKET_HANDLE) NetUtils::CloseSocket(s2);
            if (s3 != INVALID_SOCKET_HANDLE && s3 != s2) NetUtils::CloseSocket(s3);
        });
        
        g_SlaveFocused = false;
        
        {
            std::lock_guard<std::mutex> lock(g_MirrorListLock);
            g_MirrorList.clear();
        }
    }
    
    if (g_Sock != INVALID_SOCKET_HANDLE) { 
        NetUtils::CloseSocket(g_Sock); g_Sock = INVALID_SOCKET_HANDLE; 
    }

    g_Latency = 0;
    
    InitOverlay();
    UpdateUI();
    mapWidget->update();
    
    statusLabel->setText(T("连接已断开"));
    startBtn->setEnabled(true);
    ipEdit->setEnabled(true);
    portEdit->setEnabled(true);
    modeTcpBtn->setEnabled(true);
    modeBtBtn->setEnabled(true);

    g_Running = true;
    startListening();
}

void ControlWindow::onTransferCancelled(uint32_t taskId) {
    MDC_LOG_INFO(LogTag::TRANS, "Local UI triggered cancel for task %u", taskId);
    if (m_transferWidgets.count(taskId) && m_transferWidgets[taskId]) {
        m_transferWidgets[taskId]->setFinished();
        m_transferWidgets[taskId]->deleteLater();
        m_transferWidgets.erase(taskId);
    }
    {
        std::lock_guard<std::mutex> lock(g_TaskMutex);
        if (g_TransferTasks.count(taskId)) {
            auto task = g_TransferTasks[taskId];
            task->cancelled = true;
            task->receivingFiles.clear();
            if (g_Context->FileLockMgr) {
                for (const auto& kv : task->tempFilePaths) {
                    g_Context->FileLockMgr->UnlockPath(kv.second);
                }
            }
            task->tempFilePaths.clear();
        }
        g_TransferTasks.erase(taskId);
    }

    char pkt[5];
    pkt[0] = 19;
    unsigned int nT = htonl(taskId);
    memcpy(pkt + 1, &nT, 4);
    
    if (g_ClientSock == INVALID_SOCKET_HANDLE) { 
        std::lock_guard<std::mutex> lk(g_SlaveListLock);
        for(auto& s : g_SlaveList) {
             if (s->connected) {
                 std::lock_guard<std::mutex> netLock(s->sendLock);
                 send(s->sock, pkt, 5, 0);
             }
        }
    } else { 
        std::lock_guard<std::mutex> lock(g_SockLock);
        if (g_ClientSock != INVALID_SOCKET_HANDLE) send(g_ClientSock, pkt, 5, 0);
    }
}

void ControlWindow::onTransferPaused(uint32_t taskId, bool paused) {
    MDC_LOG_INFO(LogTag::TRANS, "Local UI triggered pause for task %u paused %d", taskId, paused);

    if (!paused) { 
        bool isBluetooth = false;
        if (g_ClientSock == INVALID_SOCKET_HANDLE) { 
            std::lock_guard<std::mutex> lk(g_SlaveListLock);
            for (auto& s : g_SlaveList) {
                if (s->connected && s->fileSock != INVALID_SOCKET_HANDLE && s->fileSock == s->btFileSock) {
                    isBluetooth = true;
                    break;
                }
            }
        } else { 
            if (g_ClientFileSock != INVALID_SOCKET_HANDLE && g_ClientFileSock == g_ClientBtFileSock) {
                isBluetooth = true;
            }
        }

        if (isBluetooth) {
            FileTransferWidget* targetWidget = nullptr;
            if (m_transferWidgets.count(taskId) && m_transferWidgets[taskId]) {
                targetWidget = m_transferWidgets[taskId];
            }

            if (targetWidget && !targetWidget->hasShownBtWarning()) {
                QSettings settings("MDControl", "Settings");
                bool suppressWarning = settings.value("SuppressBtWarning", false).toBool();
                if (!suppressWarning) {
                    QMessageBox msgBox(targetWidget);
                    msgBox.setWindowTitle(T("提示"));
                    msgBox.setText(T("当前TCP网络已断开，是否继续使用蓝牙传输文件？\n（蓝牙传输速度较慢）"));
                    msgBox.setIcon(QMessageBox::Question);
                    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                    msgBox.setDefaultButton(QMessageBox::Yes);
                    
                    QCheckBox* cb = new QCheckBox(T("不再提示"));
                    msgBox.setCheckBox(cb);
                    
                    int ret = msgBox.exec();
                    if (cb->isChecked()) {
                        settings.setValue("SuppressBtWarning", true);
                    }
                    
                    if (ret == QMessageBox::No) {
                        targetWidget->setPaused(true);
                        return; 
                    }
                }
                targetWidget->setBtWarningShown(true);
            }
        }
    }

    if (m_transferWidgets.count(taskId) && m_transferWidgets[taskId]) {
        m_transferWidgets[taskId]->setPaused(paused);
    }
    
    {
        std::lock_guard<std::mutex> lock(g_TaskMutex);
        if (g_TransferTasks.count(taskId)) g_TransferTasks[taskId]->paused = paused;
    }

    char pkt[6];
    pkt[0] = 22;
    unsigned int nT = htonl(taskId);
    memcpy(pkt + 1, &nT, 4);
    pkt[5] = (char)(paused ? 1 : 0);
    
    if (g_ClientSock == INVALID_SOCKET_HANDLE) { 
        std::lock_guard<std::mutex> lk(g_SlaveListLock);
        for(auto& s : g_SlaveList) {
             if (s->connected) {
                 std::lock_guard<std::mutex> netLock(s->sendLock);
                 send(s->sock, pkt, 6, 0);
             }
        }
    } else { 
        std::lock_guard<std::mutex> lock(g_SockLock);
        if (g_ClientSock != INVALID_SOCKET_HANDLE) send(g_ClientSock, pkt, 6, 0);
    }
}

void ControlWindow::reconnectSlave(int idx) {
    MDC_LOG_INFO(LogTag::UI, "User clicked reconnect for slave index: %d", idx);
    std::string addr;
    bool isBT = false;
    std::string name;

    {
        std::lock_guard<std::mutex> lock(g_SlaveListLock);
        if (idx >= 0 && idx < (int)g_SlaveList.size()) {
            auto& ctx = g_SlaveList[idx];
            addr = ctx->connectAddress;
            isBT = ctx->isBluetooth;
            name = ctx->name;
            if (ctx->connected) return; 
        } else return;
    }

    if (!isBT && !SystemUtils::HasNetworkConnectivity()) {
        QMessageBox::warning(this, T("提示"), T("网络未连接或不可用"));
        return;
    }

    RunInQThread([=](){
        if (!isBT) {
            SOCKET sockControl = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            SOCKADDR_IN sa = { 0 }; sa.sin_family = AF_INET; sa.sin_addr.s_addr = inet_addr(addr.c_str()); sa.sin_port = htons(g_TargetPort);
            if (::connect(sockControl, (SOCKADDR*)&sa, sizeof(sa)) == SOCKET_ERROR) {
                return;
            }
            NetUtils::SetNoDelay((SocketHandle)sockControl);
            
            SOCKET sockFile = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (::connect(sockFile, (SOCKADDR*)&sa, sizeof(sa)) == SOCKET_ERROR) {
                closesocket(sockControl);
                return;
            }

            if (!AuthMaster((SocketHandle)sockControl, addr)) {
                closesocket(sockControl);
                closesocket(sockFile);
                QMetaObject::invokeMethod(g_MainObject,[=]() {
                    QMessageBox::critical((QWidget*)g_MainObject, T("配对失败"), T("验证未通过或已取消。"));
                });
                return;
            }

            char m = 1; send(sockControl, &m, 1, 0);
            
            StartMasterWithSockets((SocketHandle)sockControl, (SocketHandle)sockFile, name, addr, false);
            std::shared_ptr<SlaveCtx> ctx;
            {
                std::lock_guard<std::mutex> lock(g_SlaveListLock);
                for (auto& c : g_SlaveList) {
                    if (c->connectAddress == addr && c->isBluetooth == false) {
                        ctx = c; break;
                    }
                }
            }
            
            QMetaObject::invokeMethod(g_MainObject, [=]() {
                bool isFirst = false;
                { std::lock_guard<std::mutex> lock(g_SlaveListLock); int cnt=0; for(auto& c:g_SlaveList) if(c->connected) cnt++; if(cnt==1) isFirst = true; }
                if (isFirst) {
                    RunInQThread(SenderThread);
                    StartClipboardSenderThread();
                }
                if (ctx) {
                    StartReceiverThread(ctx);
                    StartLatencyThread(ctx);
                }
            });

        } else {
            if (g_Context->BluetoothMgr) {
                if (!g_Context->BluetoothMgr->IsAvailable()) {
                    QMetaObject::invokeMethod(g_MainObject,[=]() {
                        QMessageBox::warning((QWidget*)g_MainObject, T("提示"), T("蓝牙未打开或不可用"));
                    });
                    return;
                }
                g_Context->BluetoothMgr->StopScan();
                Sleep(300);
                
                unsigned long long bthAddr = StrToBthAddr(addr);
                InterfaceSocketHandle sControl = g_Context->BluetoothMgr->Connect(bthAddr, 4);
                if ((SocketHandle)sControl != INVALID_SOCKET_HANDLE) {
                     InterfaceSocketHandle sFile = g_Context->BluetoothMgr->Connect(bthAddr, 5);

                     if (!AuthMaster((SocketHandle)sControl, name)) {
                         NetUtils::CloseSocket((SocketHandle)sControl);
                         if ((SocketHandle)sFile != INVALID_SOCKET_HANDLE) NetUtils::CloseSocket((SocketHandle)sFile);
                         QMetaObject::invokeMethod(g_MainObject,[=]() {
                             QMessageBox::critical((QWidget*)g_MainObject, T("配对失败"), T("验证未通过或已取消。"));
                         });
                         return;
                     }
                     
                     StartMasterWithSockets((SocketHandle)sControl, (SocketHandle)sFile, name, addr, true);
                     
                     std::shared_ptr<SlaveCtx> ctx;
                     {
                         std::lock_guard<std::mutex> lock(g_SlaveListLock);
                         for (auto& c : g_SlaveList) {
                             if (c->connectAddress == addr && c->isBluetooth == true) {
                                 ctx = c; break;
                             }
                         }
                     }
                     
                     if (ctx) {
                         ctx->tcpFileFailed = true;
                     }
                     
                     char handshakePkt = 20; 
                     send((SocketHandle)sControl, &handshakePkt, 1, 0);
                     
                     QMetaObject::invokeMethod(g_MainObject,[=]() {
                         bool isFirst = false;
                         { std::lock_guard<std::mutex> lock(g_SlaveListLock); int cnt=0; for(auto& c:g_SlaveList) if(c->connected) cnt++; if(cnt==1) isFirst = true; }
                         if (isFirst) {
                             RunInQThread(SenderThread);
                             StartClipboardSenderThread();
                         }
                         if (ctx) {
                             StartReceiverThread(ctx);
                             StartLatencyThread(ctx);
                         }
                     });
                }
            }
        }
    });
}

void ControlWindow::customEvent(QEvent *event) {
    if (event->type() == MDControlEvent_ClipboardUpdate) {
        ClipboardEvent* ce = static_cast<ClipboardEvent*>(event);
        if (ce->text != g_LastClipText) {
            g_LastClipText = ce->text;
            g_IgnoreClipUpdate = true;
            QApplication::clipboard()->setText(QString::fromUtf8(ce->text.c_str(), (int)ce->text.length()));
            g_IgnoreClipUpdate = false;
            
            if (g_ClientSock == INVALID_SOCKET_HANDLE) {
                MasterSendClipboard(ce->text);
            }
        }
    }
    else if (event->type() == MDControlEvent_SlaveConnected) {
        SlaveConnectedEvent* se = static_cast<SlaveConnectedEvent*>(event);
        g_ClientSock = (SocketHandle)se->sock;
        g_ClientFileSock = (SocketHandle)se->fileSock; 
        
        if (SystemUtils::IsBluetoothSocket(g_ClientSock)) {
            g_ClientBtFileSock = g_ClientFileSock;
            g_IsBluetoothConn = true;
        } else {
            g_ClientBtFileSock = INVALID_SOCKET_HANDLE;
            g_IsBluetoothConn = false;
        }
        
        mapWidget->update();
        
        g_SlaveFocused = false;
        if (g_Context->Clipboard) {
            g_Context->Clipboard->Init([](std::string s){ SlaveSendClipboard(s); },[](const std::vector<std::string>& paths) {  }
            );
            g_Context->Clipboard->StartMonitor();
        }
        StartNetworkReceiverThread();
        InitOverlay();
        UpdateUI(); 
        
        statusLabel->setText(T("已作为被控端连接"));
        startBtn->setEnabled(false);
    }
    else if (event->type() == MDControlEvent_Disconnected) {
        if (g_ClientSock != INVALID_SOCKET_HANDLE || g_ClientFileSock != INVALID_SOCKET_HANDLE) { 
            auto s1 = g_ClientSock;
            auto s2 = g_ClientFileSock;
            auto s3 = g_ClientBtFileSock;
            g_ClientSock = INVALID_SOCKET_HANDLE;
            g_ClientFileSock = INVALID_SOCKET_HANDLE;
            g_ClientBtFileSock = INVALID_SOCKET_HANDLE;
            
            g_SlaveFocused = false;
            {
                std::lock_guard<std::mutex> lock(g_MirrorListLock);
                g_MirrorList.clear();
            }
            InitOverlay();
            UpdateUI(); 
            
            RunInQThread([s1, s2, s3]() {
                if (s1 != INVALID_SOCKET_HANDLE) NetUtils::CloseSocket(s1);
                if (s2 != INVALID_SOCKET_HANDLE) NetUtils::CloseSocket(s2);
                if (s3 != INVALID_SOCKET_HANDLE && s3 != s2) NetUtils::CloseSocket(s3);
            });
            
            if (g_MainObject) {
                QMetaObject::invokeMethod(g_MainObject,[](){
                    ((ControlWindow*)g_MainObject)->statusLabel->setText(T("连接已断开"));
                    ((ControlWindow*)g_MainObject)->startListening();
                });
            }
        } else { 
            bool activeDisconnected = false;
            
            {
                std::lock_guard<std::mutex> lock(g_SlaveListLock);
                int activeIdx = g_ActiveSlaveIdx.load();
                
                int idx = 0;
                for (auto& ctx : g_SlaveList) {
                    if (!ctx->connected) {
                        if(ctx->sock != INVALID_SOCKET_HANDLE) {
                            NetUtils::CloseSocket(ctx->sock);
                            ctx->sock = INVALID_SOCKET_HANDLE;
                        }
                        if(ctx->fileSock != INVALID_SOCKET_HANDLE) {
                            NetUtils::CloseSocket(ctx->fileSock);
                            ctx->fileSock = INVALID_SOCKET_HANDLE;
                        }
                        if(ctx->btFileSock != INVALID_SOCKET_HANDLE) {
                            NetUtils::CloseSocket(ctx->btFileSock);
                            ctx->btFileSock = INVALID_SOCKET_HANDLE;
                        }
                        
                        if (idx == activeIdx) {
                            activeDisconnected = true;
                        }
                    }
                    ++idx;
                }
                if (activeDisconnected) g_ActiveSlaveIdx = -1;
            } 

            if (activeDisconnected) {
                g_IsRemote = false;
                g_ActiveSlaveIdx = -1; 
                g_Locked = false;
                
                int wx = g_LocalW / 2, wy = g_LocalH / 2;
                SystemUtils::SetCursorPos(wx, wy);

                UpdateUI();
            }
            
            mapWidget->update();
            
            bool hasConn = false;
            {
                std::lock_guard<std::mutex> lock(g_SlaveListLock);
                for(auto& ctx : g_SlaveList) if(ctx->connected) hasConn = true;
            }
            if (!hasConn) {
                if (g_MainObject) {
                    QMetaObject::invokeMethod(g_MainObject,[](){
                        ((ControlWindow*)g_MainObject)->startListening();
                    });
                }
            }
        }
    }
    else if (event->type() == MDControlEvent_PrepareFileDownload) {
        int connectedSlaves = 0;
        {
            std::lock_guard<std::mutex> lk(g_SlaveListLock);
            for(auto& s : g_SlaveList) if(s->connected) connectedSlaves++;
        }
        if (connectedSlaves > 1) {
            std::lock_guard<std::mutex> tLock(g_TaskMutex);
            if (!g_TransferTasks.empty()) {
                QMessageBox::warning(this, T("提示"), T("多设备连接时，同时只能进行一个文件传输任务。"));
                return;
            }
        }

        SetMasterDownloadTarget(); 

        auto sCtx = g_RemoteFileSource.lock();
        if (sCtx && sCtx->fileSock != INVALID_SOCKET_HANDLE) {
            g_RemoteFilesAvailable = false; 
            
            uint32_t taskId = g_NextTaskId++;
            auto task = std::make_shared<FileTransferTask>();
            task->taskId = taskId;
            task->isSender = false;
            task->deviceName = sCtx->name;
            task->targetPath = g_MasterTargetBasePath;
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
            reqPkt.insert(reqPkt.end(), task->targetPath.begin(), task->targetPath.end());
            
            send(sCtx->fileSock, reqPkt.data(), reqPkt.size(), 0);
            
            QCoreApplication::postEvent(this, new FileTransferPreparingEvent(taskId, QString::fromStdString(task->deviceName), QString::fromStdString(task->targetPath)));
        }
    }
    else if (event->type() == MDControlEvent_FileTransferPreparing) {
        FileTransferPreparingEvent* pe = static_cast<FileTransferPreparingEvent*>(event);
        uint32_t tid = pe->taskId;
        if (!m_transferWidgets.count(tid)) {
            FileTransferWidget* w = new FileTransferWidget(tid, pe->deviceName, pe->targetPath, 0, 0);
            w->show();
            m_transferWidgets[tid] = w;
            m_transferStats[tid] = {0, SystemUtils::GetTimeMS(), 0.0, 0};
            
            connect(w, &FileTransferWidget::cancelled, this,[this, tid](){ this->onTransferCancelled(tid); });
            connect(w, &FileTransferWidget::pauseToggled, this,[this, tid](bool p){ this->onTransferPaused(tid, p); });
        }
        if (m_transferWidgets[tid]) {
            m_transferWidgets[tid]->updateTraversalProgress(0, 0);
        }
    }
    else if (event->type() == MDControlEvent_FileTransferTraversal) {
        FileTransferTraversalEvent* te = static_cast<FileTransferTraversalEvent*>(event);
        if (m_transferWidgets.count(te->taskId) && m_transferWidgets[te->taskId]) {
            m_transferWidgets[te->taskId]->updateTraversalProgress(te->totalSize, te->fileCount);
        }
    }
    else if (event->type() == MDControlEvent_FileTransferStart) {
        FileTransferStartEvent* fe = static_cast<FileTransferStartEvent*>(event);
        uint32_t tid = fe->taskId;
        
        if (!m_transferWidgets.count(tid)) {
            FileTransferWidget* w = new FileTransferWidget(tid, fe->deviceName, fe->targetPath, fe->totalSize, fe->fileCount);
            w->show();
            m_transferWidgets[tid] = w;
            m_transferStats[tid] = {0, SystemUtils::GetTimeMS(), 0.0, 0};

            connect(w, &FileTransferWidget::cancelled, this,[this, tid](){ this->onTransferCancelled(tid); });
            connect(w, &FileTransferWidget::pauseToggled, this,[this, tid](bool p){ this->onTransferPaused(tid, p); });
        } else {
            if (m_transferWidgets[tid]) m_transferWidgets[tid]->setTotalInfo(fe->totalSize, fe->fileCount);
        }
        
        m_transferStats[tid].lastBytes = 0;
        m_transferStats[tid].lastTime = SystemUtils::GetTimeMS();
        m_transferStats[tid].currentSpeed = 0.0;
        m_transferStats[tid].lastUiUpdate = 0;
    }
    else if (event->type() == MDControlEvent_FileTransferProgress) {
        FileTransferProgressEvent* fe = static_cast<FileTransferProgressEvent*>(event);
        if (m_transferWidgets.count(fe->taskId) && m_transferWidgets[fe->taskId]) {
            uint32_t now = SystemUtils::GetTimeMS();
            TransferStats& stats = m_transferStats[fe->taskId];
            
            if (now - stats.lastTime >= 200) { 
                double bytesDiff = (double)(fe->currentBytes - stats.lastBytes);
                double timeDiff = (now - stats.lastTime) / 1000.0;
                double instantSpeed = 0.0;

                bool isJump = false;
                if (bytesDiff < 0) isJump = true;
                if (stats.lastBytes == 0 && bytesDiff > 5 * 1024 * 1024) isJump = true; 
                if (timeDiff > 0 && (bytesDiff / timeDiff) > 5000.0 * 1024 * 1024) isJump = true;

                if (isJump) {
                    stats.lastBytes = fe->currentBytes;
                    stats.lastTime = now;
                } else {
                    if (timeDiff > 0) instantSpeed = bytesDiff / timeDiff; 
                    
                    if (stats.currentSpeed == 0.0) {
                        stats.currentSpeed = instantSpeed;
                    } else {
                        if (timeDiff >= 1.0 || instantSpeed < stats.currentSpeed * 0.3) {
                            stats.currentSpeed = stats.currentSpeed * 0.2 + instantSpeed * 0.8;
                        } else if (instantSpeed < stats.currentSpeed) {
                            stats.currentSpeed = stats.currentSpeed * 0.5 + instantSpeed * 0.5;
                        } else {
                            stats.currentSpeed = stats.currentSpeed * 0.8 + instantSpeed * 0.2;
                        }
                    }
                    
                    stats.lastBytes = fe->currentBytes;
                    stats.lastTime = now;
                }
            }

            if (now - stats.lastUiUpdate >= 50) {
                m_transferWidgets[fe->taskId]->updateProgress(fe->currentBytes, stats.currentSpeed, fe->fileName, fe->currentFileIdx);
                stats.lastUiUpdate = now;
            }
        }
    }
    else if (event->type() == MDControlEvent_FileTransferEnd) {
        FileTransferEndEvent* ee = static_cast<FileTransferEndEvent*>(event);
        if (m_transferWidgets.count(ee->taskId) && m_transferWidgets[ee->taskId]) {
            m_transferWidgets[ee->taskId]->setFinished();
            m_transferWidgets[ee->taskId]->deleteLater();
            m_transferWidgets.erase(ee->taskId);
        }
        {
            std::lock_guard<std::mutex> lock(g_TaskMutex);
            g_TransferTasks.erase(ee->taskId);
        }
    }
    else if (event->type() == MDControlEvent_FileTransferCancel) {
        FileTransferCancelEvent* ce = static_cast<FileTransferCancelEvent*>(event);
        if (m_transferWidgets.count(ce->taskId) && m_transferWidgets[ce->taskId]) {
            m_transferWidgets[ce->taskId]->setFinished();
            m_transferWidgets[ce->taskId]->deleteLater();
            m_transferWidgets.erase(ce->taskId);
        }
        {
            std::lock_guard<std::mutex> lock(g_TaskMutex);
            g_TransferTasks.erase(ce->taskId);
        }
    }
    else if (event->type() == MDControlEvent_TransferPause) {
        TransferPauseEvent* pe = static_cast<TransferPauseEvent*>(event);
        if (m_transferWidgets.count(pe->taskId) && m_transferWidgets[pe->taskId]) {
            m_transferWidgets[pe->taskId]->setPaused(pe->isPaused);
        }
        {
            std::lock_guard<std::mutex> lock(g_TaskMutex);
            if (g_TransferTasks.count(pe->taskId)) g_TransferTasks[pe->taskId]->paused = pe->isPaused;
        }
    }
    else if (event->type() == MDControlEvent_AuthRequirePin) {
        AuthRequirePinEvent* ae = static_cast<AuthRequirePinEvent*>(event);
        QString text = ShowPinDialog(ae->name);
        
        auto* syncPtr = static_cast<std::shared_ptr<PinSync>*>(ae->promise);
        if (syncPtr) {
            std::shared_ptr<PinSync> sync = *syncPtr;
            delete syncPtr;

            {
                std::lock_guard<std::mutex> lock(sync->mtx);
                if (text.length() == 6) {
                    sync->pin = text.toStdString();
                } else {
                    sync->pin = "";
                }
                sync->done = true;
            }
            sync->cv.notify_one();
        }
    }
}

ConflictListModel::ConflictListModel(const std::vector<ConflictItem>& items, QObject* parent)
    : QAbstractListModel(parent), m_items(items) {
}

int ConflictListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_items.size();
}

QVariant ConflictListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_items.size()) return QVariant();
    if (role == Qt::UserRole) {
        return QVariant::fromValue(reinterpret_cast<void*>(const_cast<ConflictItem*>(&m_items[index.row()])));
    }
    return QVariant();
}

bool ConflictListModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (index.isValid() && role == Qt::EditRole) {
        m_items[index.row()].keepSource = value.toBool();
        emit dataChanged(index, index, {Qt::DisplayRole});
        return true;
    }
    return false;
}

void ConflictListModel::setAllKeepSource(bool keepSource) {
    beginResetModel();
    for (auto& item : m_items) {
        item.keepSource = keepSource;
    }
    endResetModel();
}

void ConflictItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->save();
    
    QRect r = option.rect;
    painter->fillRect(r, option.state & QStyle::State_Selected ? option.palette.highlight() : option.palette.base());
    painter->setPen(option.palette.midlight().color());
    painter->drawRect(r.adjusted(2, 2, -2, -2));
    
    void* ptr = index.data(Qt::UserRole).value<void*>();
    if (!ptr) { painter->restore(); return; }
    ConflictItem* item = reinterpret_cast<ConflictItem*>(ptr);

    auto formatTime = [](uint64_t t) {
        if (t == 0) return T("未知时间");
        return QDateTime::fromSecsSinceEpoch(t).toString("yyyy/MM/dd HH:mm");
    };
    auto formatSize = [](uint64_t s) {
        if (s < 1024) return T("%1 字节").arg(s);
        else if (s < 1024 * 1024) return QString("%1 KB").arg(s / 1024.0, 0, 'f', 1);
        else return QString("%1 MB").arg(s / 1024.0 / 1024.0, 0, 'f', 1);
    };

    QRect nameRect(r.x() + 10, r.y() + 5, r.width() - 20, 25);
    painter->setPen(option.palette.text().color());
    QFont f = painter->font();
    f.setBold(true);
    painter->setFont(f);
    QString elidedName = painter->fontMetrics().elidedText(item->fileName, Qt::ElideLeft, nameRect.width());
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);
    f.setBold(false);
    painter->setFont(f);

    int halfW = r.width() / 2;
    QRect leftRect(r.x() + 10, r.y() + 30, halfW - 20, r.height() - 35);
    QRect rightRect(r.x() + halfW + 10, r.y() + 30, halfW - 20, r.height() - 35);

    auto drawSide = [&](const QRect& sideRect, bool isChecked, const QString& title, const QString& timeStr, const QString& sizeStr) {
        QStyleOptionButton cbOpt;
        cbOpt.rect = QRect(sideRect.x(), sideRect.y() + 5, 20, 20);
        cbOpt.state = QStyle::State_Enabled | (isChecked ? QStyle::State_On : QStyle::State_Off);
        cbOpt.palette = option.palette;
        QApplication::style()->drawControl(QStyle::CE_CheckBox, &cbOpt, painter);

        painter->setPen(option.palette.link().color());
        painter->drawText(QRect(sideRect.x() + 25, sideRect.y() + 5, sideRect.width() - 25, 20), Qt::AlignLeft | Qt::AlignVCenter, title);

        painter->setPen(option.palette.buttonText().color());
        painter->drawText(QRect(sideRect.x(), sideRect.y() + 30, sideRect.width(), 20), Qt::AlignLeft | Qt::AlignVCenter, timeStr);
        painter->drawText(QRect(sideRect.x(), sideRect.y() + 50, sideRect.width(), 20), Qt::AlignLeft | Qt::AlignVCenter, sizeStr);
    };

    drawSide(leftRect, item->keepSource, T("文件来自于 源"), formatTime(item->sourceTime), formatSize(item->sourceSize));
    drawSide(rightRect, !item->keepSource, T("文件已位于 目标"), formatTime(item->targetTime), formatSize(item->targetSize));

    painter->restore();
}

QSize ConflictItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return QSize(200, 110);
}

bool ConflictItemDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) {
    if (event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        QRect r = option.rect;
        int halfW = r.width() / 2;
        if (me->pos().y() > r.y() + 30) {
            if (me->pos().x() < r.x() + halfW) {
                model->setData(index, true, Qt::EditRole);
            } else {
                model->setData(index, false, Qt::EditRole);
            }
            return true;
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

DuplicateDecisionDialog::DuplicateDecisionDialog(const std::vector<ConflictItem>& items, QWidget* parent) : QDialog(parent) {
    setWindowTitle(T("共 %1 个文件冲突").arg(items.size()));
    resize(600, 500);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QLabel* lblHeader = new QLabel(T("<b>你要保留哪些文件？</b><br>请勾选你要保留的文件。"));
    mainLayout->addWidget(lblHeader);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* btnAllSrc = new QPushButton(T("全部保留源文件"));
    QPushButton* btnAllTgt = new QPushButton(T("全部保留目标文件"));
    btnLayout->addWidget(btnAllSrc);
    btnLayout->addWidget(btnAllTgt);
    btnLayout->addStretch();
    mainLayout->addLayout(btnLayout);

    QListView* listView = new QListView(this);
    m_model = new ConflictListModel(items, this);
    listView->setModel(m_model);
    listView->setItemDelegate(new ConflictItemDelegate(this));
    listView->setSelectionMode(QAbstractItemView::NoSelection);
    mainLayout->addWidget(listView);

    connect(btnAllSrc, &QPushButton::clicked, [this]() { m_model->setAllKeepSource(true); });
    connect(btnAllTgt, &QPushButton::clicked, [this]() { m_model->setAllKeepSource(false); });

    QDialogButtonBox* bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    mainLayout->addWidget(bbox);

    connect(bbox, &QDialogButtonBox::accepted, [this]() {
        const auto& finalItems = m_model->getItems();
        for (const auto& it : finalItems) {
            if (!it.keepSource) {
                m_skippedIndices.insert(it.idx);
            }
        }
        this->accept();
    });
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}