#include "Common.h" 
#include "WinFileLockMgr.h"
#include <vector>
#include <algorithm>

WinFileLockMgr::~WinFileLockMgr() {
    UnlockAll();
}

void WinFileLockMgr::LockPath(const std::string& path, bool allowWrite) {
    MDC_LOG_TRACE(LogTag::FILE, "LockPath requested length: %zu allowWrite: %d", path.length(), allowWrite);
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
            MDC_LOG_DEBUG(LogTag::FILE, "Locked path length: %zu writeAllowed: %d", normPath.length(), allowWrite);
        } else {
            MDC_LOG_ERROR(LogTag::FILE, "Failed to lock path length: %zu error: %d", normPath.length(), GetLastError());
        }
    }
}

void WinFileLockMgr::UnlockPath(const std::string& path) {
    MDC_LOG_TRACE(LogTag::FILE, "UnlockPath requested length: %zu", path.length());
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_locks.find(path);
    if (it != m_locks.end()) {
        CloseHandle(it->second);
        m_locks.erase(it);
        MDC_LOG_DEBUG(LogTag::FILE, "Unlocked path length: %zu", path.length());
    }
}

void WinFileLockMgr::UnlockAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    int count = 0;
    for (auto& pair : m_locks) {
        if (pair.second != INVALID_HANDLE_VALUE) {
            CloseHandle(pair.second);
            count++;
        }
    }
    m_locks.clear();
    MDC_LOG_INFO(LogTag::FILE, "UnlockAll released %d locks", count);
}