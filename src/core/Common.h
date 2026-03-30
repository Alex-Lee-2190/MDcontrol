#ifndef COMMON_H
#define COMMON_H

#include "SocketCompat.h" 
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <algorithm>
#include <functional>
#include <queue>
#include <condition_variable>
#include <vector>
#include <map>
#include <set>
#include <debugapi.h>
#include <cstdarg>
#include <memory> 
#include <cstdint>
#include <filesystem> 
#include "Interfaces.h"

// 日志相关的枚举
enum class LogLevel { TRACE, DEBUG, INFO, WARN, ERR };
enum class LogTag { SYS, NET, BTH, AUTH, KVM, FILE, TRANS, UI, LEGACY };

void InitLogger();
void ShutdownLogger();
void AsyncLogImpl(LogLevel level, LogTag tag, const char* file, const char* format, ...);

#define MDC_LOG_TRACE(tag, fmt, ...) AsyncLogImpl(LogLevel::TRACE, tag, __FILE__, fmt, ##__VA_ARGS__)
#define MDC_LOG_DEBUG(tag, fmt, ...) AsyncLogImpl(LogLevel::DEBUG, tag, __FILE__, fmt, ##__VA_ARGS__)
#define MDC_LOG_INFO(tag,  fmt, ...) AsyncLogImpl(LogLevel::INFO,  tag, __FILE__, fmt, ##__VA_ARGS__)
#define MDC_LOG_WARN(tag,  fmt, ...) AsyncLogImpl(LogLevel::WARN,  tag, __FILE__, fmt, ##__VA_ARGS__)
#define MDC_LOG_ERROR(tag, fmt, ...) AsyncLogImpl(LogLevel::ERR,   tag, __FILE__, fmt, ##__VA_ARGS__)

// Global variables declaration

class MDControlContext;
class InputCore;

extern MDControlContext* g_Context;

extern std::atomic<bool> g_Running;
extern std::mutex g_SockLock;
extern std::atomic<bool> g_IgnoreClipUpdate;
extern std::string g_LastClipText;

extern int g_LogLevel;
extern bool g_LogToFile;
extern std::string g_FallbackTransferPath;
extern bool g_RememberPos;
extern int g_Language; // 0: Auto, 1: zh_CN, 2: en_US

extern int g_HkToggleMod;
extern int g_HkToggleVk;
extern int g_HkLockMod;
extern int g_HkLockVk;
extern int g_HkDisconnMod;
extern int g_HkDisconnVk;
extern int g_HkExitMod;
extern int g_HkExitVk;

extern std::string g_MyPubKey;
extern std::string g_MyPrivKey;

struct FileClipInfo {
    std::string name;       
    uint64_t size;
    std::string sourcePath; 
};

extern std::vector<FileClipInfo> g_LastCopiedFiles; 
extern std::vector<FileClipInfo> g_ServerTransferList; 
extern std::vector<std::string> g_ReceivedRoots;

extern std::mutex g_FileClipMutex;
extern std::atomic<bool> g_HasFileUpdate; 

// Multi-task file transfer structure
struct FileTransferTask {
    uint32_t taskId;
    bool isSender;
    std::atomic<bool> cancelled{false};
    std::atomic<bool> paused{false};
    std::string deviceName;
    std::string targetPath;

    std::vector<FileClipInfo> activeTransferList;
    int totalFilesToReceive = 0;
    std::atomic<int> filesReceived{0};
    std::atomic<int> currentFileIdx{0};
    uint64_t totalTransferSize = 0;
    std::atomic<uint64_t> currentTransferBytes{0};
    std::vector<std::string> receivedRoots;
    std::map<int, std::shared_ptr<std::ofstream>> receivingFiles;
    std::map<int, std::string> tempFilePaths;
    std::set<int> skippedFiles; 
    std::set<int> pendingDuplicateIndices; 
    std::function<void()> startReceiverTransferCallback; 

    std::vector<FileClipInfo> serverTransferList;
    uint64_t serverTotalSize = 0;
    std::atomic<uint64_t> serverSentBytes{0};

    std::atomic<uint32_t> senderSession{0};
    std::atomic<bool> recoveryRequested{false};
    
    std::atomic<uint32_t> lastActiveTime{0};
    std::atomic<uint64_t> currentFileRecvSize{0};
    std::atomic<bool> errorPrompting{false}; 

    // I/O Decoupling & Backpressure mechanism
    std::mutex diskMutex;
    std::condition_variable diskCv;
    std::queue<std::function<void()>> diskQueue;
    std::atomic<uint64_t> pendingDiskBytes{0};
    std::atomic<bool> diskThreadActive{false};

    ~FileTransferTask() {
        stopDiskThread();
    }

    void enqueueDiskTask(int bytes, std::function<void()> taskFunc) {
        {
            std::lock_guard<std::mutex> lock(diskMutex);
            diskQueue.push(taskFunc);
            pendingDiskBytes += bytes;
        }
        diskCv.notify_one();
    }

    void startDiskThread() {
        if (diskThreadActive.exchange(true)) return;
        std::thread([this]() {
            while (diskThreadActive) {
                std::function<void()> t;
                {
                    std::unique_lock<std::mutex> lock(diskMutex);
                    diskCv.wait(lock, [this] { return !diskQueue.empty() || !diskThreadActive || cancelled.load(); });
                    if (cancelled.load() || (!diskThreadActive && diskQueue.empty())) break;
                    t = diskQueue.front();
                    diskQueue.pop();
                }
                if (t) t();
            }
        }).detach();
    }

    void stopDiskThread() {
        diskThreadActive = false;
        diskCv.notify_all();
    }
};

extern std::map<uint32_t, std::shared_ptr<FileTransferTask>> g_TransferTasks;
extern std::mutex g_TaskMutex;
extern std::atomic<uint32_t> g_NextTaskId;
extern std::mutex g_ClientFileSendLock;

extern std::atomic<int> g_RemoteMouseX; 
extern std::atomic<int> g_RemoteMouseY; 
extern std::atomic<int> g_MirrorActiveIdx; 
extern std::atomic<int> g_MirrorTx; 
extern std::atomic<int> g_MirrorTy; 

struct MirrorCtx {
    int w, h;
    double logicalX, logicalY;
    std::string name;
    bool paused;          
    bool tcpFileFailed;   
    std::string sysProps; 
    double scale;
};
extern std::vector<MirrorCtx> g_MirrorList;
extern std::mutex g_MirrorListLock;
extern int g_MirrorMasterW, g_MirrorMasterH;
extern double g_MirrorMasterScale;

struct InputPkt {
    char data[13];
    int len;
    int targetIdx; 
};
extern std::queue<InputPkt> g_InputQueue;
extern std::mutex g_InputQueueLock;

struct SlaveCtx {
    SocketHandle sock;     
    SocketHandle fileSock; 
    SocketHandle btFileSock; 
    int width;
    int height;
    double logicalX;
    double logicalY;
    double ratioX;
    double ratioY;
    std::string name;
    std::atomic<uint32_t> latency;
    bool connected;
    std::string connectAddress;
    bool isBluetooth;
    std::atomic<bool> paused{false}; 
    std::atomic<bool> tcpFileFailed{false}; 
    std::atomic<uint32_t> lastFilePongTime{0}; 
    std::string sysProps;            
    double scale;
    std::string lastSentTopo;
    std::mutex sendLock; 
    std::mutex fileSendLock;

    SlaveCtx() : sock(INVALID_SOCKET_HANDLE), fileSock(INVALID_SOCKET_HANDLE), btFileSock(INVALID_SOCKET_HANDLE), width(0), height(0), logicalX(0.0), logicalY(0.0), ratioX(1.0), ratioY(1.0), latency(0), connected(true), isBluetooth(false), scale(1.0) {}
};

extern std::vector<std::shared_ptr<SlaveCtx>> g_SlaveList;
extern std::mutex g_SlaveListLock;
extern std::atomic<int> g_ActiveSlaveIdx; 

extern std::string g_TargetIP;
extern int g_TargetPort;
extern double g_LogicalX;     
extern double g_LogicalY;   
extern SocketHandle g_Sock;      
extern std::atomic<bool> g_IsRemote;
extern std::atomic<bool> g_Locked;
extern uint32_t g_LastSwitchTime;
extern std::atomic<uint32_t> g_Latency;

extern std::weak_ptr<SlaveCtx> g_RemoteFileSource;
extern std::atomic<bool> g_RemoteFilesAvailable;
extern std::string g_MasterTargetBasePath; 

extern std::string g_MyName;
extern std::string g_MySysProps;
extern std::string g_MasterSysProps;

class QObject;
extern QObject* g_MainObject;

extern std::atomic<int> g_CurTx;
extern std::atomic<int> g_CurTy;
extern std::atomic<bool> g_HasUpdate;
extern std::queue<std::string> g_ClipboardQueue;
extern std::mutex g_ClipboardMutex;
extern std::condition_variable g_ClipboardCond;

extern SocketHandle g_ClientSock;     
extern SocketHandle g_ClientFileSock; 
extern SocketHandle g_ClientBtFileSock; 
extern std::atomic<bool> g_SlaveFocused; 

extern int g_LocalW, g_LocalH;
extern double g_LocalScale;

extern int g_SlaveW, g_SlaveH;
extern double g_RatioX, g_RatioY;

extern std::atomic<bool> g_IsBluetoothConn; 
extern std::atomic<uint32_t> g_LastFilePingTime; 

// Function prototypes

unsigned long long StrToBthAddr(const std::string& str); 
void SenderThread();
void StartReceiverThread(std::shared_ptr<SlaveCtx> ctx); 
void StartClipboardSenderThread();
void StartLatencyThread(std::shared_ptr<SlaveCtx> ctx); 
void SendEvent(int type, int p1, int p2, int targetIdx = -1); 
void MasterSendClipboard(const std::string& text);
void MasterSendFileClipboard(const std::vector<std::string>& paths);
void InitOverlay();
void UpdateUI();

void DebugLog(const char* format, ...);
void SlaveSendClipboard(const std::string& text_utf8);
void SlaveSendFileClipboard(const std::vector<std::string>& paths);
void StartNetworkReceiverThread();
void InitPlatform(); 

void SetMasterDownloadTarget();

#endif