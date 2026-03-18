#include "Common.h"
#include "SystemUtils.h"
#include "KvmContext.h"
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

void ClipboardSenderThreadFunc() {
    while (g_Running) {
        std::string text_to_send;
        {
            std::unique_lock<std::mutex> lock(g_ClipboardMutex);
            g_ClipboardCond.wait(lock,[]{ return !g_ClipboardQueue.empty() || !g_Running; });
            if (!g_Running) break;
            text_to_send = g_ClipboardQueue.front();
            g_ClipboardQueue.pop();
        }
        if (!text_to_send.empty()) {
            int len = text_to_send.length();
            std::vector<char> packet(1 + 4 + len);
            packet[0] = 3;
            unsigned int nLen = htonl(len);
            memcpy(packet.data() + 1, &nLen, 4);
            memcpy(packet.data() + 5, text_to_send.data(), len);

            std::vector<std::shared_ptr<SlaveCtx>> targets;
            {
                std::lock_guard<std::mutex> listLock(g_SlaveListLock);
                for (auto& ctx : g_SlaveList) {
                    if (ctx->connected && ctx->sock != INVALID_SOCKET_HANDLE) targets.push_back(ctx);
                }
            }
            for (auto& ctx : targets) {
                std::lock_guard<std::mutex> lock(ctx->sendLock);
                send(ctx->sock, packet.data(), packet.size(), 0);
            }
        }
    }
}

void StartClipboardSenderThread() {
    std::thread(ClipboardSenderThreadFunc).detach();
}

void MasterSendClipboard(const std::string& text) {
    g_HasFileUpdate = false;
    g_RemoteFilesAvailable = false; // Force revoke remote file lock
    g_RemoteFileSource.reset();     // Clear outdated source pointer
    {
        std::lock_guard<std::mutex> lock(g_ClipboardMutex);
        g_ClipboardQueue.push(text);
    }
    g_ClipboardCond.notify_one();
}

void MasterSendFileClipboard(const std::vector<std::string>& paths) {
    DebugLog("[MASTER] MasterSendFileClipboard called with %d paths\n", (int)paths.size());
    g_HasFileUpdate = true;
    g_RemoteFilesAvailable = false; // Force revoke remote file lock
    g_RemoteFileSource.reset();     // Clear outdated source pointer
    std::vector<char> pkt;
    pkt.push_back(12); 
    
    {
        std::lock_guard<std::mutex> lock(g_FileClipMutex);
        g_LastCopiedFiles.clear();

        for(const auto& p : paths) {
            try {
                fs::path sourcePath = fs::u8path(p);
                if (!fs::exists(sourcePath)) continue;
                
                std::string absPath = sourcePath.u8string();
                std::string netName = sourcePath.filename().u8string();
                uint64_t size = 0;
                if (!fs::is_directory(sourcePath)) {
                    size = fs::file_size(sourcePath);
                } else {
                    netName += "/"; 
                }
                
                g_LastCopiedFiles.push_back({netName, size, absPath});
                DebugLog("[MASTER] Adding ROOT: %s\n", absPath.c_str());
            } catch (...) {}
        }

        unsigned int numFiles = htonl((unsigned int)g_LastCopiedFiles.size());
        pkt.insert(pkt.end(), (char*)&numFiles, (char*)&numFiles + 4);

        for (const auto& info : g_LastCopiedFiles) {
            unsigned int nameLen = htonl((unsigned int)info.name.length());
            pkt.insert(pkt.end(), (char*)&nameLen, (char*)&nameLen + 4);
            pkt.insert(pkt.end(), info.name.begin(), info.name.end());
            uint64_t netSize = htonll(info.size);
            pkt.insert(pkt.end(), (char*)&netSize, (char*)&netSize + 8);
        }
    }
    
    std::vector<std::shared_ptr<SlaveCtx>> targets;
    {
        std::lock_guard<std::mutex> listLock(g_SlaveListLock);
        for (auto& ctx : g_SlaveList) {
            if (ctx->connected && ctx->sock != INVALID_SOCKET_HANDLE) targets.push_back(ctx);
        }
    }
    for (auto& ctx : targets) {
        std::lock_guard<std::mutex> lock(ctx->sendLock);
        NetUtils::SendAll(ctx->sock, pkt.data(), pkt.size());
    }
}