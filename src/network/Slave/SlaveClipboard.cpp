#include "Common.h"
#include "SystemUtils.h"
#include "SocketCompat.h"
#include <vector>
#include <filesystem>
#include <mutex>

namespace fs = std::filesystem;

void SlaveSendClipboard(const std::string& text_utf8) {
    if (g_ClientSock == INVALID_SOCKET_HANDLE) return;
    int len = text_utf8.length();
    if (len == 0) return;

    std::vector<char> packet(5 + len);
    packet[0] = 3; 
    unsigned int nLen = htonl(len);
    memcpy(packet.data() + 1, &nLen, 4);
    memcpy(packet.data() + 5, text_utf8.data(), len);

    std::lock_guard<std::mutex> lock(g_SockLock);
    send(g_ClientSock, packet.data(), packet.size(), 0);
}

void SlaveSendFileClipboard(const std::vector<std::string>& paths) {
    if (g_ClientSock == INVALID_SOCKET_HANDLE) return;
    DebugLog("[SLAVE] SlaveSendFileClipboard called with %d paths\n", (int)paths.size());
    
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
                DebugLog("[SLAVE] Adding ROOT: %s\n", absPath.c_str());
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
    
    std::lock_guard<std::mutex> lock(g_SockLock);
    NetUtils::SendAll(g_ClientSock, pkt.data(), pkt.size());
}