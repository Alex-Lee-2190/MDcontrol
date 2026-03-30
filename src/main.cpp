#include "Common.h"
#include "MainWindow.h"
#include "SystemUtils.h"
#include "SocketCompat.h" 
#include "InputCore.h"
#include "KvmContext.h"
#include "IconDrawer.h"
#include <QtWidgets/QApplication>
#include <QtCore/QSettings>
#include <vector>
#include <chrono>
#include <iomanip>
#include <sstream>

// Global variables

MDControlContext* g_Context = nullptr;

std::atomic<bool> g_Running(true);
std::mutex g_SockLock;
std::atomic<bool> g_IgnoreClipUpdate(false);
std::string g_LastClipText = ""; 

int g_LogLevel = 1;
bool g_LogToFile = true;
std::string g_FallbackTransferPath = "C:\\Users\\Public\\Downloads";
bool g_RememberPos = true;
int g_Language = 0; // 0: Auto, 1: zh_CN, 2: en_US
int g_ThemeMode = 0; // 0: Auto, 1: Light, 2: Dark

int g_HkToggleMod = 0;
int g_HkToggleVk = 0;
int g_HkLockMod = 0;
int g_HkLockVk = 0;
int g_HkDisconnMod = 0;
int g_HkDisconnVk = 0;
int g_HkExitMod = 0;
int g_HkExitVk = 0x7B; 

std::string g_MyPubKey = "";
std::string g_MyPrivKey = "";

std::vector<FileClipInfo> g_LastCopiedFiles;
std::vector<FileClipInfo> g_ServerTransferList; 
std::vector<std::string> g_ReceivedRoots; 

std::mutex g_FileClipMutex;
std::atomic<bool> g_HasFileUpdate(false);

std::atomic<int> g_RemoteMouseX(0);
std::atomic<int> g_RemoteMouseY(0);
std::atomic<int> g_MirrorActiveIdx(-1);
std::atomic<int> g_MirrorTx(0);
std::atomic<int> g_MirrorTy(0);

std::vector<MirrorCtx> g_MirrorList;
std::mutex g_MirrorListLock;
int g_MirrorMasterW = 1920, g_MirrorMasterH = 1080; 
double g_MirrorMasterScale = 1.0;

std::vector<std::shared_ptr<SlaveCtx>> g_SlaveList; 
std::mutex g_SlaveListLock;
std::atomic<int> g_ActiveSlaveIdx(-1);

SocketHandle g_Sock = INVALID_SOCKET_HANDLE;
std::atomic<bool> g_IsRemote(false);
std::atomic<bool> g_Locked(false);
uint32_t g_LastSwitchTime = 0;
std::atomic<uint32_t> g_Latency(0);

std::weak_ptr<SlaveCtx> g_RemoteFileSource;
std::atomic<bool> g_RemoteFilesAvailable(false);

std::string g_MyName = "";
std::string g_MySysProps = "";
std::string g_MasterSysProps = "";

QObject* g_MainObject = nullptr;
double g_LogicalX = 0.0;
double g_LogicalY = 0.0; 
std::atomic<int> g_CurTx(0);
std::atomic<int> g_CurTy(0);
std::atomic<bool> g_HasUpdate(false);
std::string g_TargetIP = "";
int g_TargetPort = 5000;
std::queue<std::string> g_ClipboardQueue;
std::mutex g_ClipboardMutex;
std::condition_variable g_ClipboardCond;

SocketHandle g_ClientSock = INVALID_SOCKET_HANDLE;
SocketHandle g_ClientFileSock = INVALID_SOCKET_HANDLE; 
SocketHandle g_ClientBtFileSock = INVALID_SOCKET_HANDLE; 
std::atomic<bool> g_SlaveFocused(false); 

int g_LocalW = 0, g_LocalH = 0;
double g_LocalScale = 1.0;

int g_SlaveW = 0, g_SlaveH = 0;
double g_RatioX = 1.0, g_RatioY = 1.0;

std::map<uint32_t, std::shared_ptr<FileTransferTask>> g_TransferTasks;
std::mutex g_TaskMutex;
std::atomic<uint32_t> g_NextTaskId(1);
std::mutex g_ClientFileSendLock;

std::atomic<bool> g_IsBluetoothConn(false); 
std::atomic<uint32_t> g_LastFilePingTime(0); 

// --- Logger Implementation ---
std::queue<std::string> g_LogQueue;
std::mutex g_LogMutex;
std::condition_variable g_LogCond;
std::thread g_LogThread;
std::atomic<bool> g_LogRunning(false);

static std::string GetCurrentTimestampString() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto timer = std::chrono::system_clock::to_time_t(now);
    std::tm bt = {};
#ifdef _WIN32
    localtime_s(&bt, &timer);
#else
    localtime_r(&timer, &bt);
#endif
    std::ostringstream oss;
    oss << std::put_time(&bt, "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

static std::string ExtractFileName(const char* path) {
    std::string s(path);
    size_t pos = s.find_last_of("\\/");
    if (pos != std::string::npos) s = s.substr(pos + 1);
    size_t ext = s.find_last_of(".");
    if (ext != std::string::npos) s = s.substr(0, ext);
    return s;
}

void LogWorkerThread() {
    while (g_LogRunning) {
        std::string msg;
        {
            std::unique_lock<std::mutex> lock(g_LogMutex);
            g_LogCond.wait(lock, [] { return !g_LogQueue.empty() || !g_LogRunning; });
            if (!g_LogRunning && g_LogQueue.empty()) break;
            if (!g_LogQueue.empty()) {
                msg = g_LogQueue.front();
                g_LogQueue.pop();
            }
        }
        if (!msg.empty()) {
#ifdef _WIN32
            OutputDebugStringA(msg.c_str());
#else
            printf("%s", msg.c_str());
#endif
            if (g_LogToFile) {
                SystemUtils::WriteLog(msg);
            }
        }
    }
}

void InitLogger() {
    if (g_LogRunning) return;
    g_LogRunning = true;
    g_LogThread = std::thread(LogWorkerThread);
}

void ShutdownLogger() {
    if (!g_LogRunning) return;
    g_LogRunning = false;
    g_LogCond.notify_all();
    if (g_LogThread.joinable()) {
        g_LogThread.join();
    }
}

void AsyncLogImpl(LogLevel level, LogTag tag, const char* file, const char* format, ...) {
    if (static_cast<int>(level) < g_LogLevel) return;

    std::string levelStr;
    switch(level) {
        case LogLevel::TRACE: levelStr = "TRACE"; break;
        case LogLevel::DEBUG: levelStr = "DEBUG"; break;
        case LogLevel::INFO:  levelStr = "INFO "; break;
        case LogLevel::WARN:  levelStr = "WARN "; break;
        case LogLevel::ERR:   levelStr = "ERROR"; break;
    }
    std::string tagStr;
    switch(tag) {
        case LogTag::SYS: tagStr = "SYS"; break;
        case LogTag::NET: tagStr = "NET"; break;
        case LogTag::BTH: tagStr = "BTH"; break;
        case LogTag::AUTH: tagStr = "AUTH"; break;
        case LogTag::KVM: tagStr = "KVM"; break;
        case LogTag::FILE: tagStr = "FILE"; break;
        case LogTag::TRANS: tagStr = "TRANS"; break;
        case LogTag::UI: tagStr = "UI"; break;
        case LogTag::LEGACY: tagStr = "LEGACY"; break;
        default: tagStr = "UNK"; break;
    }

    char buffer[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    std::string msgStr(buffer);
    if (!msgStr.empty() && msgStr.back() == '\n') {
        msgStr.pop_back();
    }

    std::ostringstream oss;
    oss << "[" << GetCurrentTimestampString() << "] "
        << "[T-" << SystemUtils::GetCurrentThreadId() << "] "
        << "[" << levelStr << "] "
        << "[" << tagStr << "] "
        << "<" << ExtractFileName(file) << "> "
        << msgStr << "\n";

    {
        std::lock_guard<std::mutex> lock(g_LogMutex);
        g_LogQueue.push(oss.str());
    }
    g_LogCond.notify_one();
}

void DebugLog(const char* format, ...) {
    char buffer[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    AsyncLogImpl(LogLevel::DEBUG, LogTag::LEGACY, "Legacy", "%s", buffer);
}

void QtLogMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    std::string localMsg = msg.toStdString();
    const char* file = context.file ? context.file : "Qt";
    switch (type) {
        case QtDebugMsg:
            AsyncLogImpl(LogLevel::DEBUG, LogTag::UI, file, "%s", localMsg.c_str());
            break;
        case QtInfoMsg:
            AsyncLogImpl(LogLevel::INFO, LogTag::UI, file, "%s", localMsg.c_str());
            break;
        case QtWarningMsg:
            AsyncLogImpl(LogLevel::WARN, LogTag::UI, file, "%s", localMsg.c_str());
            break;
        case QtCriticalMsg:
        case QtFatalMsg:
            AsyncLogImpl(LogLevel::ERR, LogTag::UI, file, "%s", localMsg.c_str());
            break;
    }
}

int main(int argc, char *argv[]) {
    InitLogger();
    qInstallMessageHandler(QtLogMessageHandler);
    MDC_LOG_INFO(LogTag::SYS, "Application starting");

    if (!NetUtils::InitNetwork()) {
        MDC_LOG_ERROR(LogTag::SYS, "Network initialization failed");
        SystemUtils::ShowErrorMessage("Error", "Network Init Failed");
        return -1;
    }

    g_Context = new MDControlContext();
    InitPlatform();
    g_Context->InputCore = new InputCore();

    SystemUtils::SetProcessHighPriority();
    
    QSettings settings("MDControl", "Settings");
    g_Language = settings.value("Language", 0).toInt();
    g_ThemeMode = settings.value("ThemeMode", 0).toInt();

    g_MyName = SystemUtils::GetComputerNameStr();
    g_MySysProps = SystemUtils::GetSystemProperties();

    MDC_LOG_INFO(LogTag::SYS, "Settings loaded language: %d theme: %d", g_Language, g_ThemeMode);

    QApplication app(argc, argv);

    SystemUtils::ApplyTheme(g_ThemeMode);

    app.setWindowIcon(IconDrawer::getAppIcon());

    g_FallbackTransferPath = settings.value("FallbackPath", "C:\\Users\\Public\\Downloads").toString().toStdString();
    g_LogToFile = settings.value("LogToFile", true).toBool();
    g_LogLevel = settings.value("LogLevel", 1).toInt();
    g_RememberPos = settings.value("RememberPos", true).toBool();

    g_HkToggleMod = settings.value("HkToggle_Mod", 0).toInt();
    g_HkToggleVk = settings.value("HkToggle_VK", 0).toInt();
    g_HkLockMod = settings.value("HkLock_Mod", 0).toInt();
    g_HkLockVk = settings.value("HkLock_VK", 0).toInt();
    g_HkDisconnMod = settings.value("HkDisconn_Mod", 0).toInt();
    g_HkDisconnVk = settings.value("HkDisconn_VK", 0).toInt();
    g_HkExitMod = settings.value("HkExit_Mod", 0).toInt();
    g_HkExitVk = settings.value("HkExit_VK", 0x7B).toInt();

    QSettings authSettings("MDControl", "Auth");
    g_MyPubKey = authSettings.value("PubKey", "").toString().toStdString();
    g_MyPrivKey = authSettings.value("PrivKey", "").toString().toStdString();
    if (g_MyPubKey.empty() || g_MyPrivKey.empty()) {
        g_Context->CryptoMgr->GenerateRSAKeys(g_MyPubKey, g_MyPrivKey);
        authSettings.setValue("PubKey", QString::fromStdString(g_MyPubKey));
        authSettings.setValue("PrivKey", QString::fromStdString(g_MyPrivKey));
        MDC_LOG_INFO(LogTag::AUTH, "New RSA keys generated and saved");
    } else {
        MDC_LOG_INFO(LogTag::AUTH, "RSA keys loaded");
    }

    SystemUtils::InitDPI();
    g_LocalScale = SystemUtils::GetDPIScale();
    SystemUtils::GetScreenSize(g_LocalW, g_LocalH);
    MDC_LOG_INFO(LogTag::SYS, "Display initialized scale: %.2f resolution: %dx%d", g_LocalScale, g_LocalW, g_LocalH);

    ControlWindow w;
    w.show();
    MDC_LOG_INFO(LogTag::UI, "Main window shown");

    if (g_Context->InputListener) g_Context->InputListener->Start();

    MDC_LOG_INFO(LogTag::SYS, "Entering main event loop");
    int ret = app.exec();

    MDC_LOG_INFO(LogTag::SYS, "Exiting main event loop initiating cleanup");

    g_Running = false;
    g_ClipboardCond.notify_all(); 
    
    if (g_Context->InputListener) g_Context->InputListener->Stop();

    {
        std::lock_guard<std::mutex> lock(g_SlaveListLock);
        for(auto& ctx : g_SlaveList) {
            if(ctx->sock != INVALID_SOCKET_HANDLE) NetUtils::CloseSocket(ctx->sock);
            if(ctx->fileSock != INVALID_SOCKET_HANDLE) NetUtils::CloseSocket(ctx->fileSock);
            if(ctx->btFileSock != INVALID_SOCKET_HANDLE && ctx->btFileSock != ctx->fileSock) NetUtils::CloseSocket(ctx->btFileSock);
        }
        g_SlaveList.clear();
    }
    
    if (g_Sock != INVALID_SOCKET_HANDLE) NetUtils::CloseSocket(g_Sock);
    if (g_ClientSock != INVALID_SOCKET_HANDLE) NetUtils::CloseSocket(g_ClientSock);
    if (g_ClientFileSock != INVALID_SOCKET_HANDLE) NetUtils::CloseSocket(g_ClientFileSock);
    if (g_ClientBtFileSock != INVALID_SOCKET_HANDLE && g_ClientBtFileSock != g_ClientFileSock) NetUtils::CloseSocket(g_ClientBtFileSock);
    
    delete g_Context; g_Context = nullptr;
    NetUtils::CleanupNetwork();
    
    MDC_LOG_INFO(LogTag::SYS, "Application exit code: %d", ret);
    ShutdownLogger();
    return ret;
}