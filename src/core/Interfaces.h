#ifndef INTERFACES_H
#define INTERFACES_H

#include <string>
#include <functional>
#include <vector>

// Forward declaration to avoid socket header pollution
#ifdef _WIN32
typedef unsigned long long InterfaceSocketHandle; 
#else
typedef int InterfaceSocketHandle;
#endif

struct BluetoothDevice {
    unsigned long long address;
    std::string name;
};

// Input injection interface (Slave)
class IInputInjector {
public:
    virtual ~IInputInjector() = default;
    virtual void SendMouseClick(int btn, bool down) = 0;
    virtual void SendMouseScroll(int delta) = 0;
    virtual void SendKey(int vk, int scan, bool down) = 0;
};

// Clipboard management interface (Slave)
class IClipboard {
public:
    virtual ~IClipboard() = default;
    virtual void Init(std::function<void(std::string)> onTextChange, std::function<void(const std::vector<std::string>&)> onFilesChange) = 0;
    virtual void SetLocalClipboard(const std::string& text) = 0;
    virtual void SetLocalFiles(const std::vector<std::string>& filePaths) = 0;
    virtual void StartMonitor() = 0;
};

// Input listener interface (Master)
class IInputListener {
public:
    virtual ~IInputListener() = default;
    virtual void Start() = 0;
    virtual void Stop() = 0;
};

// Bluetooth management interface
class IBluetoothMgr {
public:
    virtual ~IBluetoothMgr() = default;
    
    // Master scan
    virtual void StartScan(std::function<void(const BluetoothDevice&)> onDeviceFound, std::function<void(const std::string&)> onFinished) = 0;
    virtual void StopScan() = 0;
    
    // Master connect
    virtual InterfaceSocketHandle Connect(unsigned long long address, int port) = 0;
    
    // Slave listen on specific ports (Control + File)
    virtual std::vector<InterfaceSocketHandle> Listen(int portControl, int portFile) = 0;

    // Stop listening and release ports
    virtual void StopListen() = 0;
    
    virtual std::string GetLocalAddress() = 0;
};

// File lock interface to prevent deletion/renaming during transfer
class IFileLockMgr {
public:
    virtual ~IFileLockMgr() = default;
    // allowWrite: true=allow write (receiver), false=read-only (sender)
    virtual void LockPath(const std::string& path, bool allowWrite) = 0; 
    virtual void UnlockPath(const std::string& path) = 0;
    virtual void UnlockAll() = 0;
};

// RSA crypto and security pairing interface
class ICryptoMgr {
public:
    virtual ~ICryptoMgr() = default;
    virtual void GenerateRSAKeys(std::string& pubKey, std::string& privKey) = 0;
    virtual std::string RSAEncrypt(const std::string& pubKey, const std::string& data) = 0;
    virtual std::string RSADecrypt(const std::string& privKey, const std::string& encData) = 0;
    virtual std::string GenerateRandomString(int length) = 0;
};

#endif // INTERFACES_H