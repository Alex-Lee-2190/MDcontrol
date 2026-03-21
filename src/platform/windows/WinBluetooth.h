#ifndef WIN_BLUETOOTH_H
#define WIN_BLUETOOTH_H

#include "Interfaces.h"
#include <atomic>
#include <string>
#include <vector>

class WinBluetoothMgr : public IBluetoothMgr {
public:
    WinBluetoothMgr();
    virtual ~WinBluetoothMgr();

    bool IsAvailable() override;

    void StartScan(std::function<void(const BluetoothDevice&)> onDeviceFound, std::function<void(const std::string&)> onFinished) override;
    void StopScan() override;
    
    InterfaceSocketHandle Connect(unsigned long long address, int port) override;
    std::vector<InterfaceSocketHandle> Listen(int portControl, int portFile) override;
    void StopListen() override;
    
    std::string GetLocalAddress() override;

private:
    std::atomic<bool> m_scanning;
    std::atomic<unsigned long long> m_listeningSocket;
};

#endif