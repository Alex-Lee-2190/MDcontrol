#ifndef WIN_FILE_LOCK_MGR_H
#define WIN_FILE_LOCK_MGR_H

#include "Interfaces.h"
#include <windows.h>
#include <string>
#include <map>
#include <mutex>

class WinFileLockMgr : public IFileLockMgr {
public:
    virtual ~WinFileLockMgr();
    void LockPath(const std::string& path, bool allowWrite) override;
    void UnlockPath(const std::string& path) override;
    void UnlockAll() override;

private:
    std::map<std::string, HANDLE> m_locks;
    std::mutex m_mutex;
};

#endif