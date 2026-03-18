#ifndef SOCKET_COMPAT_H
#define SOCKET_COMPAT_H

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    
    typedef SOCKET SocketHandle;
    #define INVALID_SOCKET_HANDLE INVALID_SOCKET
    #define SOCKET_ERROR_CODE SOCKET_ERROR
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>

    typedef int SocketHandle;
    #define INVALID_SOCKET_HANDLE -1
    #define SOCKET_ERROR_CODE -1
#endif

#include <string>
#include <cstdio>
#include <cstdint>

inline uint64_t portable_htonll(uint64_t val) {
    int num = 1;
    if (*(char *)&num == 1) {
        val = ((val << 8) & 0xFF00FF00FF00FF00ULL ) | ((val >> 8) & 0x00FF00FF00FF00FFULL );
        val = ((val << 16) & 0xFFFF0000FFFF0000ULL ) | ((val >> 16) & 0x0000FFFF0000FFFFULL );
        return (val << 32) | (val >> 32);
    }
    return val;
}
#define htonll portable_htonll
#define ntohll portable_htonll


namespace NetUtils {
    inline bool InitNetwork() {
#ifdef _WIN32
        WSADATA wsaData;
        return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
        return true;
#endif
    }

    inline void CleanupNetwork() {
#ifdef _WIN32
        WSACleanup();
#endif
    }

    inline void CloseSocket(SocketHandle s) {
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
    }
    
    inline void ShutdownSocket(SocketHandle s) {
#ifdef _WIN32
        shutdown(s, SD_BOTH);
#else
        shutdown(s, SHUT_RDWR);
#endif
    }

    inline void SetNoDelay(SocketHandle s) {
#ifdef _WIN32
        int nodelay = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));
#else
        int nodelay = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
#endif
    }

    inline bool SendAll(SocketHandle s, const char* data, int len) {
        int total = 0;
        while (total < len) {
            int ret = send(s, data + total, len - total, 0);
            if (ret <= 0) return false;
            total += ret;
        }
        return true;
    }

    inline bool RecvAll(SocketHandle s, char* data, int len) {
        int total = 0;
        while (total < len) {
            int ret = recv(s, data + total, len - total, 0);
            if (ret <= 0) return false;
            total += ret;
        }
        return true;
    }
}

#endif