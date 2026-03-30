#include "Common.h"
#include "SystemUtils.h"
#include "KvmEvents.h"
#include "KvmContext.h"
#include "SlaveInternal.h"
#include <QtCore/QCoreApplication>
#include <fstream>
#include <map>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>
#include <set>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include "MainWindow.h"
#include <sys/stat.h>

namespace fs = std::filesystem;

static std::wstring ConvertUtf8ToWString(const std::string& str) {
    return SystemUtils::Utf8ToWString(str);
}

static std::string GetAppDir() {
    return SystemUtils::GetAppDir();
}

static void LaunchHashTest(const std::string& targetFile) {
    SystemUtils::LaunchHashTest(targetFile);
}

void FileNetworkReceiver() {
    std::vector<char> buffer(1024 * 1024 * 4);
    int offset = 0;
    SocketHandle mySock = g_ClientFileSock;

    while (g_Running && mySock != INVALID_SOCKET_HANDLE) {
        int ret = recv(mySock, buffer.data() + offset, buffer.size() - offset, 0);
        if (ret <= 0) break;
        int total = offset + ret;
        int processed = 0;

        while (processed < total) {
            char* ptr = buffer.data() + processed;
            int remaining = total - processed;
            if (remaining < 1) break;
            char flag = ptr[0];

            if (flag == 16) { 
                int p = 1;
                if (remaining < p + 4) break;
                unsigned int nTaskId; memcpy(&nTaskId, ptr + p, 4); p += 4;
                uint32_t taskId = ntohl(nTaskId);
                
                if (remaining < p + 4) break;
                unsigned int nDevLen; memcpy(&nDevLen, ptr + p, 4); p += 4;
                int devLen = ntohl(nDevLen);
                if (devLen < 0 || remaining < p + devLen) break;
                std::string deviceName(ptr + p, devLen); p += devLen;
                
                if (remaining < p + 4) break;
                unsigned int nPathLen; memcpy(&nPathLen, ptr + p, 4); p += 4;
                int pathLen = ntohl(nPathLen);
                if (pathLen < 0 || remaining < p + pathLen) break;
                std::string targetPath(ptr + p, pathLen); p += pathLen;
                
                processed += p;

                auto task = std::make_shared<FileTransferTask>();
                task->taskId = taskId;
                task->isSender = true;
                task->deviceName = deviceName; 
                task->targetPath = targetPath;
                {
                    std::lock_guard<std::mutex> lock(g_TaskMutex);
                    g_TransferTasks[taskId] = task;
                }

                std::vector<FileClipInfo> localRoots;
                {
                    std::lock_guard<std::mutex> lock(g_FileClipMutex);
                    localRoots = g_LastCopiedFiles;
                }

                std::thread([taskId, localRoots]() {
                    std::shared_ptr<FileTransferTask> task;
                    {
                        std::lock_guard<std::mutex> lock(g_TaskMutex);
                        if (g_TransferTasks.count(taskId)) task = g_TransferTasks[taskId];
                    }
                    if (!task) return;

                    auto localSendPkt = [&](const char* data, int len) -> bool {
                        while (g_Running && !task->cancelled) {
                            SocketHandle targetSock = g_ClientFileSock;
                            if (targetSock == INVALID_SOCKET_HANDLE) return false;
                            
                            bool sendSuccess = false;
                            {
                                std::lock_guard<std::mutex> lock(g_ClientFileSendLock);
                                if (targetSock == g_ClientFileSock) {
                                    sendSuccess = NetUtils::SendAll(targetSock, data, len);
                                } else {
                                    continue;
                                }
                            }
                            if (sendSuccess) return true;
                            if (targetSock == g_ClientFileSock && g_ClientFileSock != INVALID_SOCKET_HANDLE) {
                                if (g_ClientFileSock != g_ClientBtFileSock) {
                                    NetUtils::CloseSocket(g_ClientFileSock);
                                    g_ClientFileSock = g_ClientBtFileSock;
                                    if (!task->paused.exchange(true)) {
                                        if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new TransferPauseEvent(taskId, true));
                                    }
                                } else {
                                    g_ClientFileSock = g_ClientBtFileSock;
                                }
                            } else {
                                return false;
                            }
                        }
                        return false;
                    };

                    std::vector<FileClipInfo> expandedList;
                    std::mutex listMutex;
                    std::atomic<uint64_t> totalSizeAtomic(0);
                    std::atomic<int> traverseCountAtomic(0);
                    std::atomic<int> activeThreads(0);

                    struct Job { fs::path path; fs::path base; };
                    std::vector<Job> jobQueue;

                    for (const auto& rootInfo : localRoots) {
                        try {
                            fs::path rootPath = fs::u8path(rootInfo.sourcePath);
                            if (!fs::exists(rootPath)) continue;

                            if (fs::is_directory(rootPath)) {
                                fs::path baseDir = rootPath.parent_path();
                                if (fs::is_empty(rootPath)) {
                                    fs::path relRoot = rootPath.lexically_relative(baseDir);
                                    std::string relStr = relRoot.u8string();
                                    if (!relStr.empty() && relStr.back() != '/' && relStr.back() != '\\') relStr += "/";
                                    expandedList.push_back({relStr, 0, rootPath.u8string()});
                                    traverseCountAtomic++;
                                } else {
                                    for (const auto& entry : fs::directory_iterator(rootPath, fs::directory_options::skip_permission_denied)) {
                                        if (entry.is_directory()) {
                                            fs::path currentPath = entry.path();
                                            fs::path relPath = currentPath.lexically_relative(baseDir);
                                            std::string relStr = relPath.u8string();
                                            if (!relStr.empty() && relStr.back() != '/' && relStr.back() != '\\') relStr += "/";
                                            {
                                                std::lock_guard<std::mutex> lk(listMutex);
                                                expandedList.push_back({relStr, 0, currentPath.u8string()});
                                                traverseCountAtomic++;
                                            }
                                            jobQueue.push_back({currentPath, baseDir});
                                        } else if (entry.is_regular_file()) {
                                            fs::path currentPath = entry.path();
                                            fs::path relPath = currentPath.lexically_relative(baseDir);
                                            uint64_t fSize = entry.file_size();
                                            {
                                                std::lock_guard<std::mutex> lk(listMutex);
                                                expandedList.push_back({relPath.u8string(), fSize, currentPath.u8string()});
                                                totalSizeAtomic += fSize;
                                                traverseCountAtomic++;
                                            }
                                        }
                                    }
                                }
                            } else {
                                {
                                    std::lock_guard<std::mutex> lk(listMutex);
                                    expandedList.push_back(rootInfo);
                                    totalSizeAtomic += rootInfo.size;
                                    traverseCountAtomic++;
                                }
                            }
                        } catch (...) {}
                    }

                    std::atomic<size_t> nextJobIdx(0);
                    int threadCount = std::thread::hardware_concurrency();
                    if (threadCount < 4) threadCount = 4;
                    if (threadCount > 16) threadCount = 16;
                    
                    MDC_LOG_INFO(LogTag::TRANS, "Starting file traversal activeThreads: %d", threadCount);
                    std::vector<std::thread> workers;
                    activeThreads = threadCount;

                    for(int i=0; i<threadCount; ++i) {
                        workers.emplace_back([&]() {
                            std::vector<FileClipInfo> localBatch;
                            localBatch.reserve(1000); 

                            while(true) {
                                if (task->cancelled) return; 

                                size_t idx = nextJobIdx.fetch_add(1);
                                if (idx >= jobQueue.size()) break;

                                Job currentJob = jobQueue[idx];
                                try {
                                    for (const auto& entry : fs::recursive_directory_iterator(currentJob.path, fs::directory_options::skip_permission_denied)) {
                                        if (task->cancelled) return; 

                                        bool isFile = entry.is_regular_file();
                                        bool isDir = entry.is_directory();

                                        if (isFile || (isDir && fs::is_empty(entry))) {
                                            fs::path currentPath = entry.path();
                                            fs::path relPath = currentPath.lexically_relative(currentJob.base);
                                            std::string relStr = relPath.u8string();
                                            if (isDir) {
                                                if (!relStr.empty() && relStr.back() != '/' && relStr.back() != '\\') relStr += "/";
                                            }
                                            uint64_t fSize = isFile ? entry.file_size() : 0;

                                            localBatch.push_back({relStr, fSize, currentPath.u8string()});
                                            if (isFile) totalSizeAtomic += fSize;
                                            traverseCountAtomic++; 

                                            if (localBatch.size() >= 500) {
                                                std::lock_guard<std::mutex> lk(listMutex);
                                                expandedList.insert(expandedList.end(), localBatch.begin(), localBatch.end());
                                                localBatch.clear();
                                            }
                                        }
                                    }
                                } catch (...) {}
                            }
                            if (!localBatch.empty()) {
                                std::lock_guard<std::mutex> lk(listMutex);
                                expandedList.insert(expandedList.end(), localBatch.begin(), localBatch.end());
                            }
                            activeThreads--;
                        });
                    }

                    if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new FileTransferPreparingEvent(taskId, QString::fromStdString(task->deviceName), QString::fromStdString(task->targetPath)));
                    
                    uint32_t lastReport = SystemUtils::GetTimeMS();
                    while(activeThreads > 0) {
                        if (task->cancelled) break; 

                        uint32_t now = SystemUtils::GetTimeMS();
                        if (now - lastReport >= 500) {
                            lastReport = now;
                            int tc = traverseCountAtomic.load();
                            uint64_t ts = totalSizeAtomic.load();
                            
                            if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new FileTransferTraversalEvent(taskId, ts, tc));
                            
                            char updatePkt[17];
                            updatePkt[0] = 18; 
                            unsigned int nTaskIdP = htonl(taskId);
                            unsigned int nCount = htonl(tc);
                            uint64_t nSize = htonll(ts);
                            memcpy(updatePkt + 1, &nTaskIdP, 4);
                            memcpy(updatePkt + 5, &nCount, 4);
                            memcpy(updatePkt + 9, &nSize, 8);
                            
                            localSendPkt(updatePkt, 17);
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }

                    for(auto& t : workers) if(t.joinable()) t.join();
                    
                    if (task->cancelled) {
                        MDC_LOG_INFO(LogTag::TRANS, "File traversal cancelled for task %u", taskId);
                        return; 
                    }

                    task->serverTransferList = expandedList; 
                    task->serverTotalSize = totalSizeAtomic.load();
                    MDC_LOG_INFO(LogTag::TRANS, "File traversal complete for task %u total files: %zu total bytes: %llu", taskId, task->serverTransferList.size(), task->serverTotalSize);

                    if (g_MainObject && !task->serverTransferList.empty()) {
                        QCoreApplication::postEvent(g_MainObject, new FileTransferStartEvent(taskId, QString::fromStdString(task->deviceName), QString::fromStdString(task->targetPath), task->serverTotalSize, (int)task->serverTransferList.size()));
                    }

                    std::vector<char> pkt;
                    pkt.push_back(17); 
                    unsigned int nTaskIdP = htonl(taskId);
                    pkt.insert(pkt.end(), (char*)&nTaskIdP, (char*)&nTaskIdP + 4);
                    unsigned int numFiles = htonl((unsigned int)task->serverTransferList.size());
                    pkt.insert(pkt.end(), (char*)&numFiles, (char*)&numFiles + 4);

                    for (const auto& info : task->serverTransferList) {
                        unsigned int nameLen = htonl((unsigned int)info.name.length());
                        pkt.insert(pkt.end(), (char*)&nameLen, (char*)&nameLen + 4);
                        pkt.insert(pkt.end(), info.name.begin(), info.name.end());
                        uint64_t netSize = htonll(info.size);
                        pkt.insert(pkt.end(), (char*)&netSize, (char*)&netSize + 8);
                    }
                    
                    localSendPkt(pkt.data(), pkt.size());
                    
                }).detach();
            }
            else if (flag == 13) { 
                if (remaining < 17) break;
                unsigned int nTaskId, nFileIdx; 
                uint64_t nOffset;
                memcpy(&nTaskId, ptr + 1, 4);
                memcpy(&nFileIdx, ptr + 5, 4);
                memcpy(&nOffset, ptr + 9, 8);
                uint32_t taskId = ntohl(nTaskId);
                int fileIdx = ntohl(nFileIdx);
                uint64_t resumeOffset = ntohll(nOffset);
                processed += 17;
                
                std::thread([taskId, fileIdx, resumeOffset]() {
                    std::shared_ptr<FileTransferTask> task;
                    {
                        std::lock_guard<std::mutex> lock(g_TaskMutex);
                        if (g_TransferTasks.count(taskId)) task = g_TransferTasks[taskId];
                    }
                    if (!task || task->cancelled) {
                        return;
                    }

                    uint32_t mySession = ++task->senderSession;

                    auto localSendPkt = [&](const char* data, int len) -> bool {
                        while (g_Running && !task->cancelled && task->senderSession == mySession) {
                            SocketHandle targetSock = g_ClientFileSock;
                            if (targetSock == INVALID_SOCKET_HANDLE) return false;
                            
                            bool sendSuccess = false;
                            {
                                std::lock_guard<std::mutex> lock(g_ClientFileSendLock);
                                if (targetSock == g_ClientFileSock) {
                                    sendSuccess = NetUtils::SendAll(targetSock, data, len);
                                } else {
                                    continue;
                                }
                            }
                            if (sendSuccess) return true;
                            if (targetSock == g_ClientFileSock && g_ClientFileSock != INVALID_SOCKET_HANDLE) {
                                if (g_ClientFileSock != g_ClientBtFileSock) {
                                    NetUtils::CloseSocket(g_ClientFileSock);
                                    g_ClientFileSock = g_ClientBtFileSock;
                                    if (!task->paused.exchange(true)) {
                                        if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new TransferPauseEvent(taskId, true));
                                    }
                                } else {
                                    g_ClientFileSock = g_ClientBtFileSock;
                                }
                            } else {
                                return false;
                            }
                        }
                        return false;
                    };

                    std::string sourcePath;
                    std::string fileName = "";
                    if (fileIdx >= 0 && fileIdx < (int)task->serverTransferList.size()) {
                        sourcePath = task->serverTransferList[fileIdx].sourcePath;
                        fileName = task->serverTransferList[fileIdx].name;
                    }
                    
                    if (!sourcePath.empty()) {
                        bool isDir = (fileName.back() == '/' || fileName.back() == '\\');
                        if (isDir) {
                            char endPkt[9]; endPkt[0] = 15; 
                            unsigned int nT = htonl(taskId); unsigned int nIdx = htonl(fileIdx);
                            memcpy(endPkt + 1, &nT, 4); memcpy(endPkt + 5, &nIdx, 4);
                            localSendPkt(endPkt, 9);
                        } else {
                            std::ifstream file;
                            bool fileOpened = false;
                            while (!task->cancelled && task->senderSession == mySession) {
                                file.open(ConvertUtf8ToWString(sourcePath).c_str(), std::ios::in | std::ios::binary);
                                if (file.is_open()) {
                                    if (g_Context->FileLockMgr) g_Context->FileLockMgr->LockPath(sourcePath, false);
                                    fileOpened = true;
                                    break;
                                } else {
                                    if (task->errorPrompting.exchange(true)) {
                                        break;
                                    }

                                    bool wasPaused = task->paused.exchange(true);
                                    if (!wasPaused) {
                                        if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new TransferPauseEvent(taskId, true));
                                        char pausePkt[6]; pausePkt[0] = 22;
                                        unsigned int nT = htonl(taskId); memcpy(pausePkt + 1, &nT, 4); pausePkt[5] = 1;
                                        localSendPkt(pausePkt, 6);
                                    }

                                    int ret = -1;
                                    if (g_MainObject) {
                                        QMetaObject::invokeMethod(g_MainObject, [&]() {
                                            ControlWindow* w = (ControlWindow*)g_MainObject;
                                            QWidget* parentWidget = w;
                                            if (w->m_transferWidgets.count(taskId) && w->m_transferWidgets[taskId]) {
                                                parentWidget = w->m_transferWidgets[taskId];
                                            }
                                            QMessageBox msgBox(parentWidget);
                                            msgBox.setWindowTitle(T("文件读取失败"));
                                            msgBox.setText(T("无法读取文件:\n%1\n可能文件被删除或磁盘已断开。").arg(QString::fromStdString(sourcePath)));
                                            msgBox.setIcon(QMessageBox::Critical);
                                            QPushButton* btnRetry = msgBox.addButton(T("重试"), QMessageBox::AcceptRole);
                                            QPushButton* btnSkip = msgBox.addButton(T("跳过"), QMessageBox::RejectRole);
                                            QPushButton* btnCancel = msgBox.addButton(T("取消传输"), QMessageBox::DestructiveRole);
                                            msgBox.exec();
                                            if (msgBox.clickedButton() == btnRetry) ret = 0;
                                            else if (msgBox.clickedButton() == btnSkip) ret = 1;
                                            else if (msgBox.clickedButton() == btnCancel) ret = 2;
                                        }, Qt::BlockingQueuedConnection);
                                    } else {
                                        ret = 2; 
                                    }

                                    task->errorPrompting = false;

                                    if (ret == 0) { 
                                        task->paused = false;
                                        if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new TransferPauseEvent(taskId, false));
                                        char resumePkt[6]; resumePkt[0] = 22;
                                        unsigned int nT = htonl(taskId); memcpy(resumePkt + 1, &nT, 4); resumePkt[5] = 0;
                                        localSendPkt(resumePkt, 6);
                                        mySession = ++task->senderSession;
                                        continue; 
                                    } else if (ret == 1) { 
                                        task->paused = false;
                                        if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new TransferPauseEvent(taskId, false));
                                        char resumePkt[6]; resumePkt[0] = 22;
                                        unsigned int nT = htonl(taskId); memcpy(resumePkt + 1, &nT, 4); resumePkt[5] = 0;
                                        localSendPkt(resumePkt, 6);
                                        mySession = ++task->senderSession;
                                        break; 
                                    } else { 
                                        task->cancelled = true;
                                        if (g_MainObject) {
                                            QMetaObject::invokeMethod(g_MainObject, [taskId](){
                                                if (g_MainObject) ((ControlWindow*)g_MainObject)->onTransferCancelled(taskId);
                                            }, Qt::QueuedConnection);
                                        }
                                        break;
                                    }
                                }
                            }
                            
                            if (fileOpened) {
                                if (resumeOffset > 0) file.seekg(resumeOffset, std::ios::beg);
                                uint64_t currentFileOffset = resumeOffset;

                                std::vector<char> chunk(1024 * 32);
                                double currentRate = 100.0 * 1024.0;
                                auto lastTokenTime = std::chrono::high_resolution_clock::now();
                                double tokens = currentRate;

                                while (true) {
                                    if (task->cancelled || task->senderSession != mySession) { 
                                        MDC_LOG_INFO(LogTag::TRANS, "Sender loop aborted task: %u", taskId);
                                        break; 
                                    }
                                    if (task->paused) {
                                        MDC_LOG_INFO(LogTag::TRANS, "Sender loop entering pause state task: %u", taskId);
                                    }
                                    while (task->paused && g_Running && !task->cancelled && task->senderSession == mySession) {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                                        lastTokenTime = std::chrono::high_resolution_clock::now();
                                    }
                                    if (task->cancelled || task->senderSession != mySession) break;

                                    SocketHandle targetSock = g_ClientFileSock;
                                    if (targetSock == INVALID_SOCKET_HANDLE) break;
                                    bool isBt = SystemUtils::IsBluetoothSocket(targetSock);
                                    int maxChunkSize = 1024 * 32;

                                    if (isBt) {
                                        uint32_t currentLatency = g_Latency.load();
                                        if (currentLatency > 75) {
                                            currentRate = std::max(1024.0, currentRate * 0.5); 
                                        } else if (currentLatency < 40) {
                                            currentRate = std::min(1024.0 * 50.0, currentRate + 2048.0); 
                                        }
                                        MDC_LOG_TRACE(LogTag::TRANS, "Bluetooth throttling active currentRate: %.2f latency: %u", currentRate, currentLatency);
                                        auto now = std::chrono::high_resolution_clock::now();
                                        double dt = std::chrono::duration<double>(now - lastTokenTime).count();
                                        lastTokenTime = now;

                                        tokens += dt * currentRate;
                                        if (tokens > currentRate) tokens = currentRate;

                                        if (tokens < 512.0) {
                                            std::this_thread::yield();
                                            continue;
                                        }
                                        maxChunkSize = std::min((int)tokens, 1024 * 1); 
                                    }

                                    file.read(chunk.data(), maxChunkSize);
                                    std::streamsize bytesRead = file.gcount();
                                    if (bytesRead > 0) {
                                        std::vector<char> pkt;
                                        pkt.push_back(14); 
                                        unsigned int nT = htonl(taskId);
                                        unsigned int nIdx = htonl(fileIdx);
                                        uint64_t nFileOffset = htonll(currentFileOffset);
                                        unsigned int nSize = htonl((unsigned int)bytesRead);
                                        pkt.insert(pkt.end(), (char*)&nT, (char*)&nT + 4);
                                        pkt.insert(pkt.end(), (char*)&nIdx, (char*)&nIdx + 4);
                                        pkt.insert(pkt.end(), (char*)&nFileOffset, (char*)&nFileOffset + 8);
                                        pkt.insert(pkt.end(), (char*)&nSize, (char*)&nSize + 4);
                                        pkt.insert(pkt.end(), chunk.data(), chunk.data() + bytesRead);
                                        
                                        if (!localSendPkt(pkt.data(), pkt.size())) break; 
                                        
                                        currentFileOffset += bytesRead;
                                        
                                        std::this_thread::yield(); 
                                        
                                        if (isBt) {
                                            tokens -= bytesRead;
                                        }

                                        task->serverSentBytes += bytesRead;
                                        if (g_MainObject) {
                                            QCoreApplication::postEvent(g_MainObject, new FileTransferProgressEvent(taskId, task->serverSentBytes.load(), fileName, fileIdx));
                                        }
                                    }
                                    if (bytesRead < maxChunkSize || !file) break;
                                }
                            }
                            if (g_Context->FileLockMgr) g_Context->FileLockMgr->UnlockPath(sourcePath);
                            char endPkt[9]; endPkt[0] = 15; 
                            unsigned int nT = htonl(taskId); unsigned int nIdx = htonl(fileIdx);
                            memcpy(endPkt + 1, &nT, 4); memcpy(endPkt + 5, &nIdx, 4);
                            localSendPkt(endPkt, 9);
                        }
                    }
                    
                    if (task->senderSession == mySession) {
                        if (fileIdx == (int)task->serverTransferList.size() - 1 || task->cancelled) {
                            if (!task->cancelled) {
                                MDC_LOG_INFO(LogTag::TRANS, "Slave sender reached file index %d of %d trigger hash test S", fileIdx, (int)task->serverTransferList.size() - 1);
                                
                                std::string testFile = "hash_test_target_S_" + std::to_string(taskId) + ".txt";
                                
                                std::string exeDir = GetAppDir();
                                std::string absTestFile = exeDir + "\\" + testFile;

                                SocketHandle targetSock = g_ClientFileSock;
                                bool useBt = targetSock != INVALID_SOCKET_HANDLE && SystemUtils::IsBluetoothSocket(targetSock);
                                std::string protocol = useBt ? "BTH" : "TCP";

                                std::ofstream ofs(absTestFile);
                                ofs << "S\n" << protocol << "\n0.0.0.0\n"; 
                                for (size_t i = 0; i < task->serverTransferList.size(); ++i) {
                                    if (task->serverTransferList[i].name.empty() || task->serverTransferList[i].name.back() == '/' || task->serverTransferList[i].name.back() == '\\') continue;
                                    ofs << task->serverTransferList[i].name << "|" << task->serverTransferList[i].sourcePath << "\n";
                                }
                                ofs.close();
                                
                                std::thread([testFile](){
                                    LaunchHashTest(testFile);
                                }).detach();
                            }

                            if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new FileTransferEndEvent(taskId));
                        }
                    }
                }).detach();
            }
            else if (flag == 18) { 
                if (remaining < 17) break;
                unsigned int nTaskId, nCount;
                uint64_t nSize;
                memcpy(&nTaskId, ptr + 1, 4);
                memcpy(&nCount, ptr + 5, 4);
                memcpy(&nSize, ptr + 9, 8);
                uint32_t taskId = ntohl(nTaskId);
                int count = ntohl(nCount);
                uint64_t size = ntohll(nSize);
                
                if (g_MainObject) {
                    QCoreApplication::postEvent(g_MainObject, new FileTransferTraversalEvent(taskId, size, count));
                }
                processed += 17;
            }
            else if (flag == 17) { 
                int p = 1;
                if (remaining < p + 4) break;
                unsigned int nTaskId; memcpy(&nTaskId, ptr + p, 4); p += 4;
                uint32_t taskId = ntohl(nTaskId);
                
                if (remaining < p + 4) break;
                unsigned int numFiles; memcpy(&numFiles, ptr+p, 4); p += 4;
                int fileCount = ntohl(numFiles);
                
                int tempOffset = p;
                bool safe = true;
                for(int i=0; i<fileCount; ++i) {
                    if (remaining < tempOffset + 4) {safe = false; break;}
                    unsigned int nameLen; memcpy(&nameLen, ptr+tempOffset, 4);
                    int len = ntohl(nameLen);
                    tempOffset += 4;
                    if (len < 0 || remaining < tempOffset + len + 8) {safe = false; break;}
                    tempOffset += (len + 8);
                }

                if (!safe) break; 

                std::shared_ptr<FileTransferTask> task;
                {
                    std::lock_guard<std::mutex> lock(g_TaskMutex);
                    if (g_TransferTasks.count(taskId)) task = g_TransferTasks[taskId];
                }

                int currentOffset = p;
                std::vector<FileClipInfo> infos;
                infos.reserve(fileCount); 

                for(int i=0; i<fileCount; ++i) {
                    unsigned int nameLen; memcpy(&nameLen, ptr+currentOffset, 4);
                    int len = ntohl(nameLen);
                    currentOffset += 4;
                    std::string name(ptr+currentOffset, len);
                    currentOffset += len;
                    uint64_t netSize; memcpy(&netSize, ptr+currentOffset, 8);
                    currentOffset += 8;
                    infos.push_back({name, ntohll(netSize), ""}); 
                }

                processed += currentOffset; 
                
                if (task) {
                    task->activeTransferList = infos;
                    task->totalFilesToReceive = infos.size();

                    std::thread([task, taskId, infos]() {
                        std::set<std::string> targetExistingPaths;
                        try {
                            fs::path targetBase = fs::u8path(task->targetPath);
                            if (fs::exists(targetBase) && fs::is_directory(targetBase)) {
                                for (const auto& entry : fs::recursive_directory_iterator(targetBase, fs::directory_options::skip_permission_denied)) {
                                    if (entry.is_regular_file()) {
                                        fs::path relPath = entry.path().lexically_relative(targetBase);
                                        std::string relStr = relPath.u8string();
                                        for (char& c : relStr) if (c == '\\') c = '/';
                                        targetExistingPaths.insert(relStr);
                                    }
                                }
                            }
                        } catch (...) {}

                        int duplicateCount = 0;
                        std::set<int> duplicateIndices;
                        for (size_t i = 0; i < infos.size(); ++i) {
                            std::string name = infos[i].name;
                            if (name.empty() || name.back() == '/' || name.back() == '\\') continue;
                            for (char& c : name) if (c == '\\') c = '/';
                            if (targetExistingPaths.count(name)) {
                                duplicateCount++;
                                duplicateIndices.insert((int)i);
                            }
                        }

                        task->startReceiverTransferCallback = [task, taskId]() {
                            for(auto& i : task->activeTransferList) task->totalTransferSize += i.size;

                            if (g_MainObject) {
                                QCoreApplication::postEvent(g_MainObject, new FileTransferStartEvent(taskId, QString::fromStdString(task->deviceName), QString::fromStdString(task->targetPath), task->totalTransferSize, task->totalFilesToReceive));
                            }

                            if (task->totalFilesToReceive > 0) {
                                task->lastActiveTime = SystemUtils::GetTimeMS();
                                task->currentFileRecvSize = 0;

                                while (task->currentFileIdx < task->totalFilesToReceive - 1 &&
                                       task->skippedFiles.count(task->currentFileIdx.load())) {
                                    task->filesReceived++;
                                    task->currentFileIdx++;
                                }

                                uint64_t reqOffset = 0;
                                if (task->skippedFiles.count(task->currentFileIdx.load())) reqOffset = 0xFFFFFFFFFFFFFFFFULL;

                                char reqPkt[17]; 
                                reqPkt[0] = 13; 
                                unsigned int nT = htonl(taskId);
                                unsigned int nIdx = htonl(task->currentFileIdx.load());
                                uint64_t nOffset = htonll(reqOffset);
                                memcpy(reqPkt + 1, &nT, 4);
                                memcpy(reqPkt + 5, &nIdx, 4);
                                memcpy(reqPkt + 9, &nOffset, 8);

                                auto localSendPkt = [task, taskId](const char* data, int len) -> bool {
                                    while (g_Running && !task->cancelled) {
                                        SocketHandle targetSock = g_ClientFileSock;
                                        if (targetSock == INVALID_SOCKET_HANDLE) return false;
                                        
                                        bool sendSuccess = false;
                                        {
                                            std::lock_guard<std::mutex> lock(g_ClientFileSendLock);
                                            if (targetSock == g_ClientFileSock) {
                                                sendSuccess = NetUtils::SendAll(targetSock, data, len);
                                            } else {
                                                continue;
                                            }
                                        }
                                        if (sendSuccess) return true;
                                        if (targetSock == g_ClientFileSock && g_ClientFileSock != INVALID_SOCKET_HANDLE) {
                                            if (g_ClientFileSock != g_ClientBtFileSock) {
                                                NetUtils::CloseSocket(g_ClientFileSock);
                                                g_ClientFileSock = g_ClientBtFileSock;
                                                if (!task->paused.exchange(true)) {
                                                    if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new TransferPauseEvent(taskId, true));
                                                }
                                            } else {
                                                g_ClientFileSock = g_ClientBtFileSock;
                                            }
                                        } else {
                                            return false;
                                        }
                                    }
                                    return false;
                                };

                                localSendPkt(reqPkt, 17);

                                std::thread([task, taskId]() {
                                    while (g_Running && !task->cancelled && task->filesReceived < task->totalFilesToReceive) {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                                        if (task->paused) {
                                            task->lastActiveTime = SystemUtils::GetTimeMS();
                                            continue;
                                        }
                                        
                                        uint32_t now = SystemUtils::GetTimeMS();
                                        if (now - task->lastActiveTime > 3000) {
                                            MDC_LOG_WARN(LogTag::TRANS, "Task %u stalled triggering recovery", taskId);
                                            task->lastActiveTime = now;
                                            task->recoveryRequested = false;
                                            
                                            uint64_t reqOffset = task->currentFileRecvSize.load();
                                            if (task->skippedFiles.count(task->currentFileIdx.load())) {
                                                reqOffset = 0xFFFFFFFFFFFFFFFFULL;
                                            }

                                            char reqPkt[17]; 
                                            reqPkt[0] = 13; 
                                            unsigned int nT = htonl(taskId);
                                            unsigned int nNextIdx = htonl(task->currentFileIdx.load());
                                            uint64_t nOffset = htonll(reqOffset);
                                            memcpy(reqPkt + 1, &nT, 4);
                                            memcpy(reqPkt + 5, &nNextIdx, 4);
                                            memcpy(reqPkt + 9, &nOffset, 8);
                                            
                                            SocketHandle targetSock = g_ClientFileSock;
                                            if (targetSock != INVALID_SOCKET_HANDLE) {
                                                std::lock_guard<std::mutex> lock(g_ClientFileSendLock);
                                                if (targetSock == g_ClientFileSock) {
                                                    NetUtils::SendAll(targetSock, reqPkt, 17);
                                                }
                                            }
                                        }
                                    }
                                }).detach();
                            }
                        };

                        if (duplicateCount > 0 && !task->cancelled) {
                            MDC_LOG_INFO(LogTag::TRANS, "Duplicate files detected count: %d task: %u", duplicateCount, taskId);
                            task->pendingDuplicateIndices = duplicateIndices;
                            
                            std::vector<char> dupPkt;
                            dupPkt.push_back(24);
                            unsigned int nT = htonl(taskId);
                            unsigned int nC = htonl(duplicateCount);
                            dupPkt.insert(dupPkt.end(), (char*)&nT, (char*)&nT + 4);
                            dupPkt.insert(dupPkt.end(), (char*)&nC, (char*)&nC + 4);
                            
                            for (int idx : duplicateIndices) {
                                unsigned int nIdx = htonl(idx);
                                dupPkt.insert(dupPkt.end(), (char*)&nIdx, (char*)&nIdx + 4);
                                
                                uint64_t tSize = 0;
                                uint64_t tTime = 0;
                                std::string name = infos[idx].name;
                                for (char& c : name) if (c == '\\') c = '/';
                                fs::path targetPath = fs::u8path(task->targetPath) / fs::u8path(name);
                                try {
                                    if (fs::exists(targetPath)) {
                                        tSize = fs::file_size(targetPath);
                                        struct stat attr;
                                        if (stat(targetPath.u8string().c_str(), &attr) == 0) {
                                            tTime = attr.st_mtime;
                                        }
                                    }
                                } catch(...) {}
                                
                                uint64_t nSize = htonll(tSize);
                                uint64_t nTime = htonll(tTime);
                                dupPkt.insert(dupPkt.end(), (char*)&nSize, (char*)&nSize + 8);
                                dupPkt.insert(dupPkt.end(), (char*)&nTime, (char*)&nTime + 8);
                            }
                            
                            SocketHandle targetSock = g_ClientFileSock;
                            if (targetSock != INVALID_SOCKET_HANDLE) {
                                std::lock_guard<std::mutex> lock(g_ClientFileSendLock);
                                if (targetSock == g_ClientFileSock) {
                                    NetUtils::SendAll(targetSock, dupPkt.data(), dupPkt.size());
                                }
                            }
                        } else {
                            if (task->startReceiverTransferCallback) task->startReceiverTransferCallback();
                        }
                    }).detach();
                }
            } else if (flag == 24) { 
                if (remaining < 9) break;
                unsigned int nTaskId, nDupCount;
                memcpy(&nTaskId, ptr + 1, 4);
                memcpy(&nDupCount, ptr + 5, 4);
                uint32_t taskId = ntohl(nTaskId);
                int dupCount = ntohl(nDupCount);
                
                int reqLen = 9 + dupCount * 20;
                if (remaining < reqLen) break;

                struct ConfInfo {
                    int idx;
                    uint64_t tSize;
                    uint64_t tTime;
                };
                std::vector<ConfInfo> rawConfs;
                int curOffset = 9;
                for (int i = 0; i < dupCount; ++i) {
                    unsigned int nIdx; memcpy(&nIdx, ptr + curOffset, 4); curOffset += 4;
                    uint64_t nSize; memcpy(&nSize, ptr + curOffset, 8); curOffset += 8;
                    uint64_t nTime; memcpy(&nTime, ptr + curOffset, 8); curOffset += 8;
                    rawConfs.push_back({(int)ntohl(nIdx), ntohll(nSize), ntohll(nTime)});
                }
                processed += reqLen;

                std::thread([taskId, rawConfs]() {
                    std::vector<ConflictItem> items;
                    std::shared_ptr<FileTransferTask> task;
                    {
                        std::lock_guard<std::mutex> lock(g_TaskMutex);
                        if (g_TransferTasks.count(taskId)) task = g_TransferTasks[taskId];
                    }
                    if (task && !task->cancelled) {
                        for (const auto& rc : rawConfs) {
                            if (rc.idx >= 0 && rc.idx < task->serverTransferList.size()) {
                                ConflictItem item;
                                item.idx = rc.idx;
                                item.fileName = QString::fromStdString(task->serverTransferList[rc.idx].name);
                                item.sourceSize = task->serverTransferList[rc.idx].size;
                                
                                std::string srcPath = task->serverTransferList[rc.idx].sourcePath;
                                struct stat attr;
                                if (stat(srcPath.c_str(), &attr) == 0) {
                                    item.sourceTime = attr.st_mtime;
                                } else {
                                    item.sourceTime = 0;
                                }
                                
                                item.targetSize = rc.tSize;
                                item.targetTime = rc.tTime;
                                item.keepSource = true;
                                items.push_back(item);
                            }
                        }
                    }

                    std::set<int> skippedIndices;
                    bool decided = false;

                    if (g_MainObject && !items.empty()) {
                        QMetaObject::invokeMethod(g_MainObject, [taskId, items, &skippedIndices, &decided]() {
                            ControlWindow* w = (ControlWindow*)g_MainObject;
                            QWidget* parentWidget = w;
                            if (w->m_transferWidgets.count(taskId) && w->m_transferWidgets[taskId]) {
                                parentWidget = w->m_transferWidgets[taskId];
                            }
                            
                            QMessageBox msgBox(parentWidget);
                            msgBox.setWindowTitle(T("文件重复"));
                            msgBox.setText(T("目标路径已有 %1 个相同名称的文件。").arg(items.size()));
                            msgBox.setIcon(QMessageBox::Question);
                            
                            QPushButton* btnOverwrite = msgBox.addButton(T("替换目标中的文件"), QMessageBox::AcceptRole);
                            QPushButton* btnSkip = msgBox.addButton(T("跳过这些文件"), QMessageBox::RejectRole);
                            QPushButton* btnDecide = msgBox.addButton(T("让我决定每个文件"), QMessageBox::ActionRole);
                            
                            msgBox.exec();
                            
                            if (msgBox.clickedButton() == btnSkip) {
                                for (const auto& item : items) skippedIndices.insert(item.idx);
                                decided = true;
                            } else if (msgBox.clickedButton() == btnOverwrite) {
                                decided = true;
                            } else if (msgBox.clickedButton() == btnDecide) {
                                DuplicateDecisionDialog decideDlg(items, parentWidget);
                                if (decideDlg.exec() == QDialog::Accepted) {
                                    skippedIndices = decideDlg.getSkippedIndices();
                                    decided = true;
                                } else {
                                    for (const auto& item : items) skippedIndices.insert(item.idx);
                                    decided = true;
                                }
                            }
                        }, Qt::BlockingQueuedConnection);
                    }
                    
                    if (decided) {
                        std::vector<char> ansPkt;
                        ansPkt.push_back(27);
                        unsigned int nT = htonl(taskId);
                        ansPkt.insert(ansPkt.end(), (char*)&nT, (char*)&nT + 4);
                        unsigned int nSkip = htonl(skippedIndices.size());
                        ansPkt.insert(ansPkt.end(), (char*)&nSkip, (char*)&nSkip + 4);
                        for (int idx : skippedIndices) {
                            unsigned int nI = htonl(idx);
                            ansPkt.insert(ansPkt.end(), (char*)&nI, (char*)&nI + 4);
                        }
                        
                        if (task && !task->cancelled) {
                            SocketHandle targetSock = g_ClientFileSock;
                            if (targetSock != INVALID_SOCKET_HANDLE) {
                                std::lock_guard<std::mutex> lock(g_ClientFileSendLock);
                                if (targetSock == g_ClientFileSock) {
                                    NetUtils::SendAll(targetSock, ansPkt.data(), ansPkt.size());
                                }
                            }
                        }
                    }
                }).detach();
            } else if (flag == 27) {
                if (remaining < 9) break;
                unsigned int nTaskId; memcpy(&nTaskId, ptr + 1, 4);
                uint32_t taskId = ntohl(nTaskId);
                unsigned int nSkip; memcpy(&nSkip, ptr + 5, 4);
                int skipCount = ntohl(nSkip);
                
                int reqLen = 9 + skipCount * 4;
                if (remaining < reqLen) break;
                
                std::set<int> skipped;
                int curOffset = 9;
                for (int i=0; i<skipCount; ++i) {
                    unsigned int nI; memcpy(&nI, ptr + curOffset, 4); curOffset += 4;
                    skipped.insert(ntohl(nI));
                }
                processed += reqLen;

                std::shared_ptr<FileTransferTask> task;
                {
                    std::lock_guard<std::mutex> lock(g_TaskMutex);
                    if (g_TransferTasks.count(taskId)) task = g_TransferTasks[taskId];
                }
                if (task && !task->cancelled) {
                    MDC_LOG_INFO(LogTag::TRANS, "Decision made skip files count: %d task: %u", (int)skipped.size(), taskId);
                    task->skippedFiles = skipped;
                    for (int idx : task->skippedFiles) {
                        if (idx < task->activeTransferList.size()) {
                            task->activeTransferList[idx].size = 0;
                        }
                    }
                    if (task->startReceiverTransferCallback) task->startReceiverTransferCallback();
                }
            } else if (flag == 14) { 
                if (remaining < 21) break;
                unsigned int nTaskId, nIdx, nSize;
                uint64_t nFileOffset;
                memcpy(&nTaskId, ptr + 1, 4);
                memcpy(&nIdx, ptr + 5, 4);
                memcpy(&nFileOffset, ptr + 9, 8);
                memcpy(&nSize, ptr + 17, 4);
                uint32_t taskId = ntohl(nTaskId);
                int fileIdx = ntohl(nIdx);
                uint64_t chunkOffset = ntohll(nFileOffset);
                int chunkSize = ntohl(nSize);
                if (chunkSize < 0 || remaining < 21 + chunkSize) break;
                
                std::shared_ptr<FileTransferTask> task;
                std::shared_ptr<std::ofstream> currentFile;
                {
                    std::lock_guard<std::mutex> lock(g_TaskMutex);
                    if (g_TransferTasks.count(taskId)) task = g_TransferTasks[taskId];

                    if (task && !task->cancelled) {
                        if (task->receivingFiles.find(fileIdx) == task->receivingFiles.end()) {
                            std::string fullPath;
                            if (fileIdx < (int)task->activeTransferList.size()) {
                                std::string relPath = task->activeTransferList[fileIdx].name;
                                if (relPath.find("..") == std::string::npos) {
                                    fs::path baseDir = fs::u8path(task->targetPath);
                                    fs::path target = baseDir / fs::u8path(relPath);
                                    try {
                                        if (target.has_parent_path()) fs::create_directories(target.parent_path());
                                        fullPath = target.u8string();
                                    } catch (...) {}
                                }
                            }
                            if (!fullPath.empty()) {
                                if (g_Context->FileLockMgr) g_Context->FileLockMgr->LockPath(fullPath, true);
                                task->receivingFiles[fileIdx] = std::make_shared<std::ofstream>(ConvertUtf8ToWString(fullPath).c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
                                task->tempFilePaths[fileIdx] = fullPath;
                            }
                        }
                        currentFile = task->receivingFiles[fileIdx];
                    }
                }

                if (task && !task->cancelled && currentFile && currentFile->is_open()) {
                    uint64_t currentRecvSize = (uint64_t)currentFile->tellp();
                    if (chunkOffset <= currentRecvSize) {
                        task->recoveryRequested = false; 
                        if (chunkOffset < currentRecvSize) {
                            currentFile->seekp(chunkOffset, std::ios::beg);
                        }
                        currentFile->write(ptr + 21, chunkSize);
                        if (chunkOffset == currentRecvSize) {
                            task->currentTransferBytes += chunkSize;
                        }
                        currentFile->seekp(0, std::ios::end);
                        
                        task->currentFileRecvSize = (uint64_t)currentFile->tellp();
                        task->lastActiveTime = SystemUtils::GetTimeMS();
                        
                        if (g_MainObject) {
                            std::string currentFileName = "";
                            if (fileIdx < (int)task->activeTransferList.size()) {
                                currentFileName = task->activeTransferList[fileIdx].name;
                            }
                            QCoreApplication::postEvent(g_MainObject, new FileTransferProgressEvent(taskId, task->currentTransferBytes.load(), currentFileName, fileIdx));
                        }
                    } else {
                        if (!task->recoveryRequested.exchange(true)) {
                            MDC_LOG_INFO(LogTag::TRANS, "Chunk offset mismatch task: %u offset: %llu current recv size: %llu requesting recovery", taskId, chunkOffset, currentRecvSize);
                            task->lastActiveTime = SystemUtils::GetTimeMS();
                            char reqPkt[17]; 
                            reqPkt[0] = 13; 
                            unsigned int nT = htonl(taskId);
                            unsigned int nNextIdx = htonl(fileIdx);
                            uint64_t nOffset = htonll(currentRecvSize);
                            memcpy(reqPkt + 1, &nT, 4);
                            memcpy(reqPkt + 5, &nNextIdx, 4);
                            memcpy(reqPkt + 9, &nOffset, 8);
                            
                            SocketHandle targetSock = g_ClientFileSock;
                            if (targetSock != INVALID_SOCKET_HANDLE) {
                                std::lock_guard<std::mutex> lock(g_ClientFileSendLock);
                                if (targetSock == g_ClientFileSock) {
                                    NetUtils::SendAll(targetSock, reqPkt, 17);
                                }
                            }
                        }
                    }
                }
                processed += 21 + chunkSize;
            } else if (flag == 15) { 
                if (remaining < 9) break;
                unsigned int nTaskId, nIdx; 
                memcpy(&nTaskId, ptr + 1, 4);
                memcpy(&nIdx, ptr + 5, 4);
                uint32_t taskId = ntohl(nTaskId);
                int fileIdx = ntohl(nIdx);
                
                std::shared_ptr<FileTransferTask> task;
                std::shared_ptr<std::ofstream> currentFile;
                bool fileExists = false;
                {
                    std::lock_guard<std::mutex> lock(g_TaskMutex);
                    if (g_TransferTasks.count(taskId)) task = g_TransferTasks[taskId];
                    if (task && task->receivingFiles.count(fileIdx)) {
                        currentFile = task->receivingFiles[fileIdx];
                        fileExists = true;
                    }
                }

                if (task) {
                    bool transferComplete = true;
                    if (fileExists && currentFile) {
                        uint64_t currentRecvSize = (uint64_t)currentFile->tellp();
                        uint64_t expectedSize = 0;
                        if (fileIdx < (int)task->activeTransferList.size()) {
                            expectedSize = task->activeTransferList[fileIdx].size;
                        }
                        
                        if (currentRecvSize < expectedSize) {
                            transferComplete = false;
                            if (!task->recoveryRequested.exchange(true)) {
                                MDC_LOG_INFO(LogTag::TRANS, "File incomplete size mismatch task: %u offset: %llu expected: %llu requesting recovery", taskId, currentRecvSize, expectedSize);
                                task->lastActiveTime = SystemUtils::GetTimeMS();
                                char reqPkt[17]; 
                                reqPkt[0] = 13; 
                                unsigned int nT = htonl(taskId);
                                unsigned int nNextIdx = htonl(fileIdx);
                                uint64_t nOffset = htonll(currentRecvSize);
                                memcpy(reqPkt + 1, &nT, 4);
                                memcpy(reqPkt + 5, &nNextIdx, 4);
                                memcpy(reqPkt + 9, &nOffset, 8);
                                
                                SocketHandle targetSock = g_ClientFileSock;
                                if (targetSock != INVALID_SOCKET_HANDLE) {
                                    std::lock_guard<std::mutex> lock(g_ClientFileSendLock);
                                    if (targetSock == g_ClientFileSock) {
                                        NetUtils::SendAll(targetSock, reqPkt, 17);
                                    }
                                }
                            }
                        } else {
                            currentFile->close();
                            std::lock_guard<std::mutex> lock(g_TaskMutex);
                            task->receivingFiles.erase(fileIdx);
                            task->currentFileRecvSize = 0;
                            task->lastActiveTime = SystemUtils::GetTimeMS();
                            if (g_Context->FileLockMgr && task->tempFilePaths.count(fileIdx)) {
                                g_Context->FileLockMgr->UnlockPath(task->tempFilePaths[fileIdx]);
                                task->tempFilePaths.erase(fileIdx);
                            }
                        }
                    } else {
                        bool isSkipped = task->skippedFiles.count(fileIdx);
                        std::string name = "";
                        if (fileIdx < (int)task->activeTransferList.size()) {
                            name = task->activeTransferList[fileIdx].name;
                        }

                        if (!name.empty() && !task->cancelled && !isSkipped) {
                            bool isDir = (name.back() == '/' || name.back() == '\\');
                            fs::path baseDir = fs::u8path(task->targetPath);
                            fs::path target = baseDir / fs::u8path(name);
                            try {
                                if (isDir) {
                                    fs::create_directories(target);
                                } else {
                                    if (target.has_parent_path()) fs::create_directories(target.parent_path());
                                    std::ofstream ofs(ConvertUtf8ToWString(target.u8string()).c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
                                }
                            } catch (...) {}
                        }
                    }
                    
                    if (transferComplete) {
                        MDC_LOG_DEBUG(LogTag::TRANS, "Slave received file completion Flag 15 for task %u files: %d of %d", taskId, task->filesReceived.load()+1, task->totalFilesToReceive);
                        task->filesReceived++;
                        
                        if (task->filesReceived == task->totalFilesToReceive) {
                            MDC_LOG_DEBUG(LogTag::TRANS, "Trigger receiver hash test on slave for task %u", taskId);
                            {
                                SocketHandle targetSock = g_ClientFileSock;
                                bool useBt = targetSock != INVALID_SOCKET_HANDLE && SystemUtils::IsBluetoothSocket(targetSock);
                                std::string protocol = useBt ? "BTH" : "TCP";
                                std::string master_addr = SystemUtils::GetPeerAddress(targetSock);

                                std::string testFile = "hash_test_target_R_" + std::to_string(taskId) + ".txt";
                                
                                std::string exeDir = GetAppDir();
                                std::string absTestFile = exeDir + "\\" + testFile;

                                std::ofstream ofs(absTestFile);
                                ofs << "R\n" << protocol << "\n" << master_addr << "\n";
                                for (size_t i = 0; i < task->activeTransferList.size(); ++i) {
                                    if (task->skippedFiles.count(i)) continue;
                                    if (task->activeTransferList[i].name.empty() || task->activeTransferList[i].name.back() == '/' || task->activeTransferList[i].name.back() == '\\') continue; 
                                    std::string absPath = task->targetPath + "\\" + task->activeTransferList[i].name;
                                    for(char& c : absPath) if(c=='/') c='\\';
                                    ofs << task->activeTransferList[i].name << "|" << absPath << "\n";
                                }
                                ofs.close();
                                
                                std::thread([testFile](){
                                    LaunchHashTest(testFile);
                                }).detach();
                            }

                            if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new FileTransferEndEvent(taskId));
                        } else {
                            if (!task->cancelled) {
                                task->currentFileIdx++;

                                while (task->currentFileIdx < task->totalFilesToReceive - 1 &&
                                       task->skippedFiles.count(task->currentFileIdx.load())) {
                                    task->filesReceived++;
                                    task->currentFileIdx++;
                                }

                                if (task->currentFileIdx < task->totalFilesToReceive) {
                                     uint64_t reqOffset = 0;
                                     if (task->skippedFiles.count(task->currentFileIdx.load())) {
                                         reqOffset = 0xFFFFFFFFFFFFFFFFULL;
                                     }

                                     char reqPkt[17]; reqPkt[0] = 13; 
                                     unsigned int nT = htonl(taskId);
                                     unsigned int nNextIdx = htonl(task->currentFileIdx.load());
                                     uint64_t nOffset = htonll(reqOffset);
                                     memcpy(reqPkt + 1, &nT, 4);
                                     memcpy(reqPkt + 5, &nNextIdx, 4);
                                     memcpy(reqPkt + 9, &nOffset, 8);
                                     
                                     task->lastActiveTime = SystemUtils::GetTimeMS();
                                     task->currentFileRecvSize = 0;

                                     auto localSendPkt = [&](const char* data, int len) -> bool {
                                         while (g_Running && !task->cancelled) {
                                             SocketHandle targetSock = g_ClientFileSock;
                                             if (targetSock == INVALID_SOCKET_HANDLE) return false;
                                             
                                             bool sendSuccess = false;
                                             {
                                                 std::lock_guard<std::mutex> lock(g_ClientFileSendLock);
                                                 if (targetSock == g_ClientFileSock) {
                                                     sendSuccess = NetUtils::SendAll(targetSock, data, len);
                                                 } else {
                                                     continue;
                                                 }
                                             }
                                             if (sendSuccess) return true;
                                             if (targetSock == g_ClientFileSock && g_ClientFileSock != INVALID_SOCKET_HANDLE) {
                                                 if (g_ClientFileSock != g_ClientBtFileSock) {
                                                     NetUtils::CloseSocket(g_ClientFileSock);
                                                     g_ClientFileSock = g_ClientBtFileSock;
                                                     if (!task->paused.exchange(true)) {
                                                         if (g_MainObject) QCoreApplication::postEvent(g_MainObject, new TransferPauseEvent(taskId, true));
                                                     }
                                                 } else {
                                                     g_ClientFileSock = g_ClientBtFileSock;
                                                 }
                                             } else {
                                                 return false;
                                             }
                                         }
                                         return false;
                                     };

                                     localSendPkt(reqPkt, 17);
                                }
                            }
                        }
                    }
                }
                processed += 9;
            } else if (flag == 30) {
                g_LastFilePingTime = SystemUtils::GetTimeMS();
                char pong[1] = { 31 };
                std::lock_guard<std::mutex> lock(g_ClientFileSendLock);
                NetUtils::SendAll(mySock, pong, 1);
                processed += 1;
            } else if (flag == 31) {
                processed += 1;
            } else {
                processed++;
            }
        }
        if (processed < total) {
            memmove(buffer.data(), buffer.data() + processed, total - processed);
            offset = total - processed;
            
            if (offset == buffer.size()) {
                buffer.resize(buffer.size() * 2);
            }
        } else offset = 0;
    }
}