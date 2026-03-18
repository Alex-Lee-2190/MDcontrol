#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ws2bth.h>
#include <ctime>
#include <windows.h>
#include <cstdint>
#include <algorithm>
#include <cstdarg>
#include <cstdlib>

#pragma comment(lib, "ws2_32.lib")

void WriteLog(const char* format, ...) {
    static std::string s_logDirPath = "";
    static std::string s_startupTime = "";

    if (s_startupTime.empty()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char timeBuf[64];
        sprintf(timeBuf, "%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        s_startupTime = timeBuf;

        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        std::string exePath(path);
        size_t pos = exePath.find_last_of("\\/");
        if (pos != std::string::npos) {
            s_logDirPath = exePath.substr(0, pos) + "\\log";
            CreateDirectoryA(s_logDirPath.c_str(), NULL);
        }
    }

    if (s_logDirPath.empty()) return;

    char fileName[128];
    sprintf(fileName, "\\HASH_TEST_%s.log", s_startupTime.c_str());
    std::string fullPath = s_logDirPath + fileName;

    char buffer[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    FILE* f = fopen(fullPath.c_str(), "a");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] %s\n", 
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buffer);
        fclose(f);
    }
}

std::wstring s2ws(const std::string& str) {
    if (str.empty()) return std::wstring();
    int sz = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), 0, 0);
    std::wstring res(sz, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &res[0], sz);
    return res;
}

// FNV-1a 64bit hash algorithm for integrity check
uint64_t calc_hash(const std::string& path) {
    std::ifstream f(s2ws(path).c_str(), std::ios::binary);
    if (!f.is_open()) return 0;
    
    uint64_t hash = 14695981039346656037ULL;
    char buf[8192];
    while (f.read(buf, sizeof(buf))) {
        for (int i = 0; i < f.gcount(); ++i) {
            hash ^= (uint8_t)buf[i];
            hash *= 1099511628211ULL;
        }
    }
    if (f.gcount() > 0) {
        for (int i = 0; i < f.gcount(); ++i) {
            hash ^= (uint8_t)buf[i];
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

std::string read_until_newline(SOCKET s) {
    std::string res;
    char c;
    while (recv(s, &c, 1, 0) > 0) {
        if (c == '\n') break;
        res += c;
    }
    return res;
}

void run_receiver(const std::string& protocol, const std::string& target_addr, const std::vector<std::pair<std::string, std::string>>& files) {
    if (files.empty()) return;

    // Sampling algorithm: >= 5%, including first and last file
    std::map<int, bool> selected;
    selected[0] = true;
    selected[files.size() - 1] = true;

    int target_count = std::max((int)(files.size() * 0.05), 2);
    srand((unsigned)time(NULL));
    int attempts = 0;
    while (selected.size() < std::min((size_t)target_count, files.size()) && attempts < 1000) {
        selected[rand() % files.size()] = true;
        attempts++;
    }

    std::vector<std::pair<std::string, std::string>> samples;
    for (auto const&[idx, val] : selected) {
        samples.push_back(files[idx]);
    }

    std::string req = std::to_string(samples.size());
    for (auto& s : samples) {
        req += "|" + s.first;
    }
    req += "\n"; 

    SOCKET sock = INVALID_SOCKET;
    if (protocol == "BTH") {
        sock = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
        SOCKADDR_BTH addr = {0};
        addr.addressFamily = AF_BTH;
        addr.port = 6;
        addr.btAddr = std::stoull(target_addr, nullptr, 16);

        int retries = 20;
        while (retries-- > 0) {
            if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0) break;
            Sleep(500);
        }
        if (retries < 0) {
            WriteLog("[HASH TEST] BTH R-side connection timeout, hash comparison aborted.");
            return;
        }
    } else {
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(5005);
        inet_pton(AF_INET, target_addr.c_str(), &addr.sin_addr);

        int retries = 20; 
        while (retries-- > 0) {
            if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0) break;
            Sleep(500);
        }
        if (retries < 0) {
            WriteLog("[HASH TEST] TCP R-side connection timeout, hash comparison aborted.");
            return;
        }
    }

    send(sock, req.c_str(), req.length(), 0);

    std::string res = read_until_newline(sock);
    
    closesocket(sock);

    if (res.empty()) {
        WriteLog("[HASH TEST] Failed to receive hash data or data is empty.");
        return;
    }

    std::vector<std::string> sender_hashes;
    size_t pos = 0;
    while ((pos = res.find('|')) != std::string::npos) {
        sender_hashes.push_back(res.substr(0, pos));
        res.erase(0, pos + 1);
    }
    if (!res.empty()) sender_hashes.push_back(res);

    bool all_pass = true;
    std::string err_details = "";

    for (size_t i = 0; i < samples.size(); ++i) {
        uint64_t l_hash = calc_hash(samples[i].second);
        char l_str[32]; 
        sprintf(l_str, "%016llx", l_hash);
        std::string lh(l_str);

        std::string sh = (i < sender_hashes.size()) ? sender_hashes[i] : "ERR";
        if (lh != sh || sh == "0000000000000000") {
            all_pass = false;
            err_details += "File: " + samples[i].first + "\n";
            err_details += "  Local Hash: " + lh + "\n";
            err_details += "  Remote Hash: " + sh + "\n";
        }
    }
    
    if (!all_pass) {
        WriteLog("[HASH TEST] WARNING: Data integrity check failed!\n%s", err_details.c_str());
    }
}

void run_sender(const std::string& protocol, const std::map<std::string, std::string>& file_map) {
    SOCKET listen_sock = INVALID_SOCKET;

    if (protocol == "BTH") {
        listen_sock = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
        SOCKADDR_BTH addr = {0};
        addr.addressFamily = AF_BTH;
        addr.port = 6;
        if (bind(listen_sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            WriteLog("[HASH TEST] BTH S-side bind channel 6 failed! (Error: %d)", WSAGetLastError());
            return;
        }
        if (listen(listen_sock, 1) == SOCKET_ERROR) {
            WriteLog("[HASH TEST] BTH S-side listen on channel 6 failed!");
            return;
        }
    } else {
        listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        int opt = 1;
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

        sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(5005);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(listen_sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            WriteLog("[HASH TEST] TCP S-side bind port 5005 failed! (Error: %d)", WSAGetLastError());
            return;
        }
        if (listen(listen_sock, 1) == SOCKET_ERROR) {
            WriteLog("[HASH TEST] TCP S-side listen on port 5005 failed!");
            return;
        }
    }
    
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(listen_sock, &read_fds);
    timeval timeout;
    timeout.tv_sec = 15;
    timeout.tv_usec = 0;

    if (select(0, &read_fds, NULL, NULL, &timeout) > 0) {
        SOCKET client = accept(listen_sock, NULL, NULL);
        if (client != INVALID_SOCKET) {
            std::string req = read_until_newline(client);

            if (!req.empty()) {
                std::vector<std::string> parts;
                size_t pos = 0;
                while ((pos = req.find('|')) != std::string::npos) {
                    parts.push_back(req.substr(0, pos));
                    req.erase(0, pos + 1);
                }
                if (!req.empty()) parts.push_back(req);

                if (!parts.empty()) {
                    int count = 0;
                    try { count = std::stoi(parts[0]); } catch(...) {}
                    
                    std::string res = "";
                    for (int i = 1; i <= count && i < (int)parts.size(); ++i) {
                        std::string rel_path = parts[i];
                        uint64_t h = 0;
                        if (file_map.count(rel_path)) {
                            h = calc_hash(file_map.at(rel_path));
                        }
                        char h_str[32]; 
                        sprintf(h_str, "%016llx", h);
                        if (i > 1) res += "|";
                        res += h_str;
                    }
                    res += "\n"; 
                    send(client, res.c_str(), res.length(), 0);
                }
            }
            
            // Wait for client to close connection to ensure all data is sent
            char dummy;
            recv(client, &dummy, 1, 0);
            
            closesocket(client);
        }
    } else {
        WriteLog("[HASH TEST] S-side connection wait timeout.");
    }
    closesocket(listen_sock);
}

int main(int argc, char* argv[]) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    if (argc < 2) {
        WriteLog("[HASH TEST] Missing config file path argument.");
        return -1;
    }

    std::string config_file = argv[1];
    std::ifstream ifs(config_file);
    if (!ifs.is_open()) {
        WriteLog("[HASH TEST] Failed to open config file: %s", config_file.c_str());
        return -1;
    }

    std::string mode;
    std::getline(ifs, mode);
    std::string protocol;
    std::getline(ifs, protocol);
    std::string target_addr;
    std::getline(ifs, target_addr);

    std::vector<std::pair<std::string, std::string>> files;
    std::map<std::string, std::string> file_map;

    std::string line;
    while (std::getline(ifs, line)) {
        size_t pos = line.find('|');
        if (pos != std::string::npos) {
            std::string rel = line.substr(0, pos);
            std::string abs = line.substr(pos + 1);
            files.push_back({rel, abs});
            file_map[rel] = abs;
        }
    }
    ifs.close();
    
    std::remove(config_file.c_str());

    if (mode == "R") {
        run_receiver(protocol, target_addr, files);
    } else if (mode == "S") {
        run_sender(protocol, file_map);
    } else {
        WriteLog("[HASH TEST] Unknown mode: %s", mode.c_str());
    }

    WSACleanup();
    return 0;
}

#ifdef _WIN32
int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow) {
    return main(__argc, __argv);
}
#endif