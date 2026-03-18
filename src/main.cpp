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

// Global variables

MDControlContext* g_Context = nullptr;

std::atomic<bool> g_Running(true);
std::mutex g_SockLock;
std::atomic<bool> g_IgnoreClipUpdate(false);
std::string g_LastClipText = ""; 

bool g_LogToFile = true;
std::string g_FallbackTransferPath = "C:\\Users\\Public\\Downloads";
bool g_RememberPos = true;
int g_Language = 0; // 0: Auto, 1: zh_CN, 2: en_US

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

void DebugLog(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

#ifdef _WIN32
    OutputDebugStringA(buffer);
#else
    printf("%s", buffer);
#endif

    if (g_LogToFile) {
        SystemUtils::WriteLog(buffer);
    }
}

int main(int argc, char *argv[]) {
    if (!NetUtils::InitNetwork()) {
        SystemUtils::ShowErrorMessage("Error", "Network Init Failed");
        return -1;
    }

    g_Context = new MDControlContext();
    InitPlatform();
    g_Context->InputCore = new InputCore();

    SystemUtils::SetProcessHighPriority();
    
    QSettings settings("MDControl", "Settings");
    g_Language = settings.value("Language", 0).toInt();

    g_MyName = SystemUtils::GetComputerNameStr();
    g_MySysProps = SystemUtils::GetSystemProperties();

    QApplication app(argc, argv);

    app.setWindowIcon(IconDrawer::getAppIcon());

    g_FallbackTransferPath = settings.value("FallbackPath", "C:\\Users\\Public\\Downloads").toString().toStdString();
    g_LogToFile = settings.value("LogToFile", true).toBool();
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
    }

    SystemUtils::InitDPI();
    g_LocalScale = SystemUtils::GetDPIScale();
    SystemUtils::GetScreenSize(g_LocalW, g_LocalH);

    ControlWindow w;
    w.show();

    if (g_Context->InputListener) g_Context->InputListener->Start();

    int ret = app.exec();

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
    return ret;
}