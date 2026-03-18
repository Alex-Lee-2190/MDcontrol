#include "WinFileLockMgr.h"
#include "Common.h" 
#include <vector>
#include <algorithm>

WinFileLockMgr::~WinFileLockMgr() {
    UnlockAll();
}

void WinFileLockMgr::LockPath(const std::string& path, bool allowWrite) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_locks.find(path) != m_locks.end()) return; 

    std::string normPath = path;
    std::replace(normPath.begin(), normPath.end(), '/', '\\');

    int wlen = MultiByteToWideChar(CP_UTF8, 0, normPath.c_str(), -1, NULL, 0);
    if (wlen > 0) {
        std::vector<wchar_t> wpath(wlen);
        MultiByteToWideChar(CP_UTF8, 0, normPath.c_str(), -1, wpath.data(), wlen);

        DWORD shareMode = FILE_SHARE_READ;
        if (allowWrite) {
            shareMode |= FILE_SHARE_WRITE; 
        }

        HANDLE hFile = CreateFileW(
            wpath.data(),
            GENERIC_READ, 
            shareMode, 
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS, 
            NULL
        );

        if (hFile == INVALID_HANDLE_VALUE) {
            hFile = CreateFileW(
                wpath.data(),
                0, 
                shareMode, 
                NULL,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS, 
                NULL
            );
        }

        if (hFile != INVALID_HANDLE_VALUE) {
            m_locks[path] = hFile; 
            DebugLog("[LOCK] Locked path: %s (WriteAllowed: %d)\n", normPath.c_str(), allowWrite);
        } else {
            DebugLog("[LOCK] Failed to lock: %s (Error: %d)\n", normPath.c_str(), GetLastError());
        }
    }
}

void WinFileLockMgr::UnlockPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_locks.find(path);
    if (it != m_locks.end()) {
        CloseHandle(it->second);
        m_locks.erase(it);
        DebugLog("[LOCK] Unlocked path: %s\n", path.c_str());
    }
}

void WinFileLockMgr::UnlockAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_locks) {
        if (pair.second != INVALID_HANDLE_VALUE) {
            CloseHandle(pair.second);
        }
    }
    m_locks.clear();
}