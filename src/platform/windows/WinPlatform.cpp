#include "Common.h"
#include "KvmContext.h"
#include "WinInputInjector.h"
#include "WinClipboard.h"
#include "WinInputListener.h"
#include "WinBluetooth.h"
#include "WinFileLockMgr.h"
#include "WinCryptoMgr.h"
#include "SystemUtils.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <thread>
#include <atomic>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

static std::string ObfuscateDiscoveryMsg(const std::string& input) {
    std::string out;
    for (size_t i = 0; i < input.length(); ++i) {
        out += (char)(input[i] ^ (0x5A + (i % 5)));
    }
    std::string hex;
    const char* hexChars = "0123456789ABCDEF";
    for (unsigned char c : out) {
        hex += hexChars[c >> 4];
        hex += hexChars[c & 0x0F];
    }
    return hex;
}

static std::string DeobfuscateDiscoveryMsg(const std::string& hex) {
    if (hex.length() % 2 != 0) return "";
    std::string out;
    for (size_t i = 0; i < hex.length(); i += 2) {
        char c1 = hex[i], c2 = hex[i+1];
        int v1 = (c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : ((c1 >= '0' && c1 <= '9') ? (c1 - '0') : 0);
        int v2 = (c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : ((c2 >= '0' && c2 <= '9') ? (c2 - '0') : 0);
        out += (char)((v1 << 4) | v2);
    }
    std::string res;
    for (size_t i = 0; i < out.length(); ++i) {
        res += (char)(out[i] ^ (0x5A + (i % 5)));
    }
    return res;
}

class WinLanDiscoveryMgr : public ILanDiscoveryMgr {
public:
    WinLanDiscoveryMgr() : m_serverRunning(false), m_serverSock(INVALID_SOCKET), m_scanRunning(false), m_scanSock(INVALID_SOCKET) {}
    ~WinLanDiscoveryMgr() override { StopServer(); StopScan(); }

    void StartServer(int port, int tcpPort, const std::string& name) override {
        if (m_serverRunning) return;
        m_serverRunning = true;
        m_serverThread = std::thread([this, port, tcpPort, name]() {
            m_serverSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (m_serverSock == INVALID_SOCKET) { 
                MDC_LOG_ERROR(LogTag::NET, "LanDiscovery StartServer socket creation failed");
                m_serverRunning = false; 
                return; 
            }
            
            int opt = 1;
            setsockopt(m_serverSock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
            setsockopt(m_serverSock, SOL_SOCKET, SO_BROADCAST, (char*)&opt, sizeof(opt));

            sockaddr_in addr = {0};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = INADDR_ANY;
            if (bind(m_serverSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
                MDC_LOG_ERROR(LogTag::NET, "LanDiscovery StartServer bind failed port: %d error: %lu", port, GetLastError());
                closesocket(m_serverSock); m_serverSock = INVALID_SOCKET; m_serverRunning = false; return;
            }

            struct ip_mreq mreq;
            mreq.imr_multiaddr.s_addr = inet_addr("239.255.43.21");
            mreq.imr_interface.s_addr = INADDR_ANY;
            if (setsockopt(m_serverSock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq)) == SOCKET_ERROR) {
                MDC_LOG_WARN(LogTag::NET, "LanDiscovery failed to join multicast group 239.255.43.21 error: %lu", GetLastError());
            }

            MDC_LOG_INFO(LogTag::NET, "LanDiscovery Server successfully started. Listening on UDP port %d for TCP port %d", port, tcpPort);

            char buf[1024];
            while (m_serverRunning && m_serverSock != INVALID_SOCKET) {
                fd_set fds; FD_ZERO(&fds); FD_SET(m_serverSock, &fds);
                timeval tv = {1, 0};
                if (select(0, &fds, NULL, NULL, &tv) > 0) {
                    sockaddr_in clientAddr; int clientLen = sizeof(clientAddr);
                    int ret = recvfrom(m_serverSock, buf, sizeof(buf) - 1, 0, (sockaddr*)&clientAddr, &clientLen);
                    if (ret > 0) {
                        buf[ret] = '\0';
                        std::string rawMsg(buf);
                        std::string msg = DeobfuscateDiscoveryMsg(rawMsg);
                        if (msg == "MDC_DISCOVER") {
                            MDC_LOG_INFO(LogTag::NET, "LanDiscovery Server received discovery from %s:%d. Sending reply.", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
                            std::string rawReply = "MDC_REPLY:" + name + ":" + std::to_string(tcpPort);
                            std::string reply = ObfuscateDiscoveryMsg(rawReply);
                            
                            if (sendto(m_serverSock, reply.c_str(), reply.length(), 0, (sockaddr*)&clientAddr, clientLen) == SOCKET_ERROR) {
                                MDC_LOG_ERROR(LogTag::NET, "Reply sendto failed error: %lu", GetLastError());
                            } else {
                                MDC_LOG_DEBUG(LogTag::NET, "Reply sent to %s:%d", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
                            }
                        }
                    }
                }
            }
        });
    }
    
    void StopServer() override {
        m_serverRunning = false;
        if (m_serverSock != INVALID_SOCKET) { closesocket(m_serverSock); m_serverSock = INVALID_SOCKET; }
        if (m_serverThread.joinable()) m_serverThread.join();
    }
    
    void StartScan(int broadcastPort, int replyPort, std::function<void(const std::string&, const std::string&, int)> onDiscovered) override {
        if (m_scanRunning) return;
        m_scanRunning = true;
        m_scanThread = std::thread([this, broadcastPort, replyPort, onDiscovered]() {
            m_scanSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (m_scanSock == INVALID_SOCKET) { 
                MDC_LOG_ERROR(LogTag::NET, "LanDiscovery StartScan socket creation failed");
                m_scanRunning = false; 
                return; 
            }
            
            int opt = 1;
            setsockopt(m_scanSock, SOL_SOCKET, SO_BROADCAST, (char*)&opt, sizeof(opt));
            setsockopt(m_scanSock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
            
            sockaddr_in addr = {0};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(0); // Use random ephemeral port for scanning
            addr.sin_addr.s_addr = INADDR_ANY;
            if (bind(m_scanSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
                MDC_LOG_ERROR(LogTag::NET, "LanDiscovery StartScan bind failed error: %lu", GetLastError());
                closesocket(m_scanSock); m_scanSock = INVALID_SOCKET; m_scanRunning = false; return;
            }

            sockaddr_in boundAddr; int boundLen = sizeof(boundAddr);
            getsockname(m_scanSock, (sockaddr*)&boundAddr, &boundLen);
            MDC_LOG_INFO(LogTag::NET, "LanDiscovery Scan started. Bound to random port %d. Target port %d", ntohs(boundAddr.sin_port), broadcastPort);
            
            std::string rawMsg = "MDC_DISCOVER";
            std::string msg = ObfuscateDiscoveryMsg(rawMsg);

            sockaddr_in mcastAddr = {0};
            mcastAddr.sin_family = AF_INET;
            mcastAddr.sin_port = htons(broadcastPort);
            mcastAddr.sin_addr.s_addr = inet_addr("239.255.43.21");
            sendto(m_scanSock, msg.c_str(), msg.length(), 0, (sockaddr*)&mcastAddr, sizeof(mcastAddr));
            MDC_LOG_DEBUG(LogTag::NET, "LanDiscovery sent multicast to 239.255.43.21:%d", broadcastPort);

            ULONG outBufLen = 0;
            GetAdaptersInfo(NULL, &outBufLen);
            if (outBufLen > 0) {
                PIP_ADAPTER_INFO pAdapterInfo = (IP_ADAPTER_INFO*)malloc(outBufLen);
                if (GetAdaptersInfo(pAdapterInfo, &outBufLen) == NO_ERROR) {
                    PIP_ADAPTER_INFO pAdapter = pAdapterInfo;
                    while (pAdapter) {
                        PIP_ADDR_STRING pIpAddrStr = &pAdapter->IpAddressList;
                        while (pIpAddrStr) {
                            std::string ip = pIpAddrStr->IpAddress.String;
                            std::string mask = pIpAddrStr->IpMask.String;
                            if (ip != "0.0.0.0" && mask != "0.0.0.0" && ip != "127.0.0.1") {
                                unsigned long ulIp = inet_addr(ip.c_str());
                                unsigned long ulMask = inet_addr(mask.c_str());
                                unsigned long ulBcast = ulIp | (~ulMask);
                                
                                sockaddr_in bcastAddr = {0};
                                bcastAddr.sin_family = AF_INET;
                                bcastAddr.sin_port = htons(broadcastPort);
                                bcastAddr.sin_addr.s_addr = ulBcast;
                                
                                sendto(m_scanSock, msg.c_str(), msg.length(), 0, (sockaddr*)&bcastAddr, sizeof(bcastAddr));
                                MDC_LOG_DEBUG(LogTag::NET, "LanDiscovery sent directed broadcast to %s:%d (from %s)", inet_ntoa(bcastAddr.sin_addr), broadcastPort, ip.c_str());
                            }
                            pIpAddrStr = pIpAddrStr->Next;
                        }
                        pAdapter = pAdapter->Next;
                    }
                }
                free(pAdapterInfo);
            }

            sockaddr_in globalBcast = {0};
            globalBcast.sin_family = AF_INET;
            globalBcast.sin_port = htons(broadcastPort);
            globalBcast.sin_addr.s_addr = INADDR_BROADCAST;
            sendto(m_scanSock, msg.c_str(), msg.length(), 0, (sockaddr*)&globalBcast, sizeof(globalBcast));
            MDC_LOG_DEBUG(LogTag::NET, "LanDiscovery sent global broadcast to 255.255.255.255:%d", broadcastPort);

            char buf[1024];
            while (m_scanRunning && m_scanSock != INVALID_SOCKET) {
                fd_set fds; FD_ZERO(&fds); FD_SET(m_scanSock, &fds);
                timeval tv = {1, 0};
                if (select(0, &fds, NULL, NULL, &tv) > 0) {
                    sockaddr_in clientAddr; int clientLen = sizeof(clientAddr);
                    int ret = recvfrom(m_scanSock, buf, sizeof(buf) - 1, 0, (sockaddr*)&clientAddr, &clientLen);
                    if (ret > 0) {
                        buf[ret] = '\0';
                        std::string rawReply(buf);
                        std::string reply = DeobfuscateDiscoveryMsg(rawReply);
                        MDC_LOG_DEBUG(LogTag::NET, "Scan socket received %d bytes from %s:%d, data: %s", ret, inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), reply.c_str());
                        if (reply.find("MDC_REPLY:") == 0) {
                            size_t pos1 = reply.find(':', 10);
                            if (pos1 != std::string::npos) {
                                std::string name = reply.substr(10, pos1 - 10);
                                int tcpPort = std::stoi(reply.substr(pos1 + 1));
                                char ipStr[INET_ADDRSTRLEN];
                                inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
                                MDC_LOG_INFO(LogTag::NET, "LanDiscovery discovered device IP: %s Name: %s Port: %d", ipStr, name.c_str(), tcpPort);
                                if (onDiscovered) onDiscovered(ipStr, name, tcpPort);
                            }
                        }
                    }
                }
            }
        });
    }
    
    void StopScan() override {
        m_scanRunning = false;
        if (m_scanSock != INVALID_SOCKET) { closesocket(m_scanSock); m_scanSock = INVALID_SOCKET; }
        if (m_scanThread.joinable()) m_scanThread.join();
    }

private:
    std::atomic<bool> m_serverRunning;
    SOCKET m_serverSock;
    std::thread m_serverThread;
    std::atomic<bool> m_scanRunning;
    SOCKET m_scanSock;
    std::thread m_scanThread;
};

void InitPlatform() {
    g_Context->InputInjector = new WinInputInjector();
    g_Context->Clipboard = new WinClipboard();
    g_Context->InputListener = new WinInputListener();
    g_Context->BluetoothMgr = new WinBluetoothMgr();
    g_Context->FileLockMgr = new WinFileLockMgr();
    g_Context->CryptoMgr = new WinCryptoMgr();
    g_Context->LanDiscoveryMgr = new WinLanDiscoveryMgr();
}