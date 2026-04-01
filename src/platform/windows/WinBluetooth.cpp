#include "WinBluetooth.h"
#include "Common.h" 
#include <winsock2.h>
#include <ws2bth.h>
#include <bluetoothapis.h>
#include <thread>
#include <cstdio>
#include <initguid.h>
#include <vector>

DEFINE_GUID(g_ServiceUuid, 0xb62c4e8d, 0x62a8, 0x4547, 0xa4, 0x33, 0xf3, 0x51, 0x6e, 0x65, 0x51, 0x16);

WinBluetoothMgr::WinBluetoothMgr() : m_scanning(false), m_listeningSocket((unsigned long long)INVALID_SOCKET) {}
WinBluetoothMgr::~WinBluetoothMgr() { StopScan(); }

bool WinBluetoothMgr::IsAvailable() {
    SOCKET s = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (s == INVALID_SOCKET) {
        MDC_LOG_ERROR(LogTag::BTH, "IsAvailable socket creation failed error: %lu", GetLastError());
        return false;
    }
    
    SOCKADDR_BTH sa = { 0 };
    sa.addressFamily = AF_BTH;
    sa.port = 0;
    if (bind(s, (SOCKADDR*)&sa, sizeof(sa)) != 0) {
        MDC_LOG_ERROR(LogTag::BTH, "IsAvailable bind failed error: %lu", GetLastError());
        closesocket(s);
        return false;
    }
    closesocket(s);
    return true;
}

void WinBluetoothMgr::StartScan(std::function<void(const BluetoothDevice&)> onDeviceFound, std::function<void(const std::string&)> onFinished) {
    if (m_scanning) return;
    m_scanning = true;
    MDC_LOG_INFO(LogTag::BTH, "Bluetooth scan started using BluetoothFindFirstDevice API");

    std::thread([this, onDeviceFound, onFinished]() {
        BLUETOOTH_DEVICE_SEARCH_PARAMS searchParams = {
            sizeof(BLUETOOTH_DEVICE_SEARCH_PARAMS),
            1, // fReturnAuthenticated: 返回已配对的设备
            1, // fReturnRemembered: 返回记住的设备
            1, // fReturnUnknown: 返回新扫描到的设备
            1, // fReturnConnected: 返回已连接的设备
            1, // fIssueInquiry: 发起新的空中扫描
            4, // cTimeoutMultiplier: 超时乘数 (4 * 1.28 秒 = 5.12 秒)
            NULL // hRadio
        };

        BLUETOOTH_DEVICE_INFO deviceInfo = { sizeof(BLUETOOTH_DEVICE_INFO), 0 };
        HBLUETOOTH_DEVICE_FIND hFind = BluetoothFindFirstDevice(&searchParams, &deviceInfo);

        if (hFind != NULL) {
            do {
                if (!m_scanning || !g_Running) break;

                BluetoothDevice dev;
                dev.address = deviceInfo.Address.ullLong;
                
                int len = WideCharToMultiByte(CP_UTF8, 0, deviceInfo.szName, -1, NULL, 0, NULL, NULL);
                if (len > 1) {
                    std::string name(len - 1, 0);
                    WideCharToMultiByte(CP_UTF8, 0, deviceInfo.szName, -1, &name[0], len, NULL, NULL);
                    dev.name = name;
                } else {
                    dev.name = "Unknown";
                }
                
                MDC_LOG_INFO(LogTag::BTH, "Bluetooth device found MAC: %012llX Name: %s", dev.address, dev.name.c_str());
                if (onDeviceFound) onDeviceFound(dev);

            } while (BluetoothFindNextDevice(hFind, &deviceInfo));
            
            BluetoothFindDeviceClose(hFind);
        } else {
            MDC_LOG_WARN(LogTag::BTH, "BluetoothFindFirstDevice returned NULL, error: %lu. No devices found.", GetLastError());
        }

        m_scanning = false;
        MDC_LOG_INFO(LogTag::BTH, "Bluetooth scan finished");
        if (onFinished) onFinished("Scan Finished");
    }).detach();
}

void WinBluetoothMgr::StopScan() {
    m_scanning = false;
}

InterfaceSocketHandle WinBluetoothMgr::Connect(unsigned long long address, int port) {
    MDC_LOG_INFO(LogTag::BTH, "Attempting Bluetooth connect to %012llX port %d", address, port);
    SOCKET s = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (s == INVALID_SOCKET) {
        MDC_LOG_ERROR(LogTag::BTH, "Bluetooth socket creation failed error: %lu", GetLastError());
        return (InterfaceSocketHandle)INVALID_SOCKET;
    }

    SOCKADDR_BTH sa = {0};
    sa.addressFamily = AF_BTH;
    sa.btAddr = address;
    sa.port = port;

    if (connect(s, (SOCKADDR*)&sa, sizeof(sa)) == SOCKET_ERROR) {
        MDC_LOG_ERROR(LogTag::BTH, "Bluetooth connect failed error: %lu", GetLastError());
        closesocket(s);
        return (InterfaceSocketHandle)INVALID_SOCKET;
    }
    
    MDC_LOG_INFO(LogTag::BTH, "Bluetooth connect successful");
    return (InterfaceSocketHandle)s;
}

void WinBluetoothMgr::StopListen() {
    SOCKET s = (SOCKET)m_listeningSocket.exchange((unsigned long long)INVALID_SOCKET);
    if (s != INVALID_SOCKET) {
        closesocket(s);
    }
}

std::vector<InterfaceSocketHandle> WinBluetoothMgr::Listen(int portControl, int portFile) {
    std::vector<InterfaceSocketHandle> clients;

    auto SetupListener = [&](int p) -> SOCKET {
        SOCKET s = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
        if (s == INVALID_SOCKET) return INVALID_SOCKET;
        SOCKADDR_BTH sa = {0};
        sa.addressFamily = AF_BTH;
        sa.port = p;
        
        int retries = 10;
        while(retries-- > 0) {
            if (bind(s, (SOCKADDR*)&sa, sizeof(sa)) == 0) break;
            Sleep(500);
        }
        if (retries < 0) {
            MDC_LOG_ERROR(LogTag::BTH, "Bluetooth bind failed port %d retries exhausted", p);
            closesocket(s); return INVALID_SOCKET;
        }

        if (listen(s, 1) != 0) {
            MDC_LOG_ERROR(LogTag::BTH, "Bluetooth listen failed port %d error: %lu", p, GetLastError());
            closesocket(s); return INVALID_SOCKET;
        }
        return s;
    };

    SOCKET sCtrl = SetupListener(portControl);
    if (sCtrl == INVALID_SOCKET) {
        MDC_LOG_ERROR(LogTag::BTH, "Failed to bind control port %d", portControl);
        return clients;
    }

    m_listeningSocket = (unsigned long long)sCtrl;

    SOCKET sFile = INVALID_SOCKET;
    if (portFile > 0) {
        sFile = SetupListener(portFile);
        if (sFile == INVALID_SOCKET) {
            MDC_LOG_ERROR(LogTag::BTH, "Failed to bind file port %d", portFile);
            
            SOCKET s = (SOCKET)m_listeningSocket.exchange((unsigned long long)INVALID_SOCKET);
            if (s != INVALID_SOCKET) closesocket(s);
            
            return clients;
        }
    }

    WSAQUERYSETW qs = { 0 };
    qs.dwSize = sizeof(qs);
    qs.lpServiceClassId = (LPGUID)&g_ServiceUuid;
    qs.lpszServiceInstanceName = (LPWSTR)L"MDControl Slave Service";
    qs.dwNameSpace = NS_BTH;
    
    SOCKADDR_BTH sockAddr = { 0 };
    int sockAddrLen = sizeof(sockAddr);
    getsockname(sCtrl, (SOCKADDR*)&sockAddr, &sockAddrLen);

    CSADDR_INFO csAddr = { 0 };
    csAddr.LocalAddr.lpSockaddr = (LPSOCKADDR)&sockAddr;
    csAddr.LocalAddr.iSockaddrLength = sizeof(sockAddr);
    csAddr.iSocketType = SOCK_STREAM;
    csAddr.iProtocol = BTHPROTO_RFCOMM;

    qs.dwNumberOfCsAddrs = 1;
    qs.lpcsaBuffer = &csAddr;
    
    if (WSASetServiceW(&qs, RNRSERVICE_REGISTER, 0) != 0) {
        MDC_LOG_ERROR(LogTag::BTH, "WSASetServiceW register failed error: %lu", GetLastError());
        SOCKET s = (SOCKET)m_listeningSocket.exchange((unsigned long long)INVALID_SOCKET);
        if (s != INVALID_SOCKET) closesocket(s);

        if (sFile != INVALID_SOCKET) closesocket(sFile);
        return clients;
    }

    MDC_LOG_INFO(LogTag::BTH, "Bluetooth service registered listening on channel %d%s", portControl, (portFile > 0) ? " and File Port" : "");
    
    SOCKET cCtrl = INVALID_SOCKET;
    while (g_Running) {
        cCtrl = accept(sCtrl, NULL, NULL);
        if (cCtrl != INVALID_SOCKET) {
            break;
        }
        
        if (m_listeningSocket.load() == (unsigned long long)INVALID_SOCKET) {
            break; 
        }
        
        MDC_LOG_WARN(LogTag::BTH, "Accept control socket aborted incidentally retrying");
        Sleep(100); 
    }

    if (cCtrl != INVALID_SOCKET) {
        MDC_LOG_INFO(LogTag::BTH, "Control socket accepted");
        clients.push_back((InterfaceSocketHandle)cCtrl);
        
        if (sFile != INVALID_SOCKET) {
            m_listeningSocket = (unsigned long long)sFile;
            
            SOCKET cFile = INVALID_SOCKET;
            while (g_Running) {
                cFile = accept(sFile, NULL, NULL);
                if (cFile != INVALID_SOCKET) {
                    break;
                }
                
                if (m_listeningSocket.load() == (unsigned long long)INVALID_SOCKET) {
                    break;
                }
                
                MDC_LOG_WARN(LogTag::BTH, "Accept file socket aborted incidentally retrying");
                Sleep(100);
            }
            
            if (cFile != INVALID_SOCKET) {
                 MDC_LOG_INFO(LogTag::BTH, "File socket accepted");
                 clients.push_back((InterfaceSocketHandle)cFile);
            } else {
                MDC_LOG_ERROR(LogTag::BTH, "Accept file socket failed error: %lu", GetLastError());
                closesocket(cCtrl);
                clients.clear();
            }
        }
    } else {
        MDC_LOG_ERROR(LogTag::BTH, "Accept control socket failed error: %lu", GetLastError());
    }

    WSASetServiceW(&qs, RNRSERVICE_DELETE, 0);
    
    m_listeningSocket.exchange((unsigned long long)INVALID_SOCKET);
    if (sCtrl != INVALID_SOCKET) closesocket(sCtrl);
    if (sFile != INVALID_SOCKET) closesocket(sFile);

    return clients;
}

std::string WinBluetoothMgr::GetLocalAddress() {
    SOCKET s = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (s == INVALID_SOCKET) return "00:00:00:00:00:00";
    SOCKADDR_BTH sa = { 0 }; sa.addressFamily = AF_BTH; sa.port = 0;
    bind(s, (SOCKADDR*)&sa, sizeof(sa));
    int len = sizeof(sa); getsockname(s, (SOCKADDR*)&sa, &len); closesocket(s);
    char buf[64]; sprintf(buf, "%012llX", sa.btAddr);
    return std::string(buf);
}