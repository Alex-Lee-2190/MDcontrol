#include <windows.h>
#include <iostream>
#include <string>
#include <netlistmgr.h>
#include <wininet.h>
#include <iphlpapi.h>
#include <cstdio>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "iphlpapi.lib")

// 定义 COM 接口 ID
const CLSID CLSID_NetworkListManager_Local = {0xDCB00C01, 0x570F, 0x4A9B, {0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B}};
const IID IID_INetworkListManager_Local = {0xDCB00000, 0x570F, 0x4A9B, {0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B}};

void PrintNetworkStatus() {
    std::cout << "========================================\n";
    std::cout << "[方法 1] WinInet API (InternetGetConnectedState)\n";
    DWORD flags;
    bool inetState = InternetGetConnectedState(&flags, 0);
    std::cout << "  -> 结果: " << (inetState ? "TRUE (有网络)" : "FALSE (无网络)") << "\n";

    std::cout << "\n[方法 2 & 3] COM INetworkListManager API\n";
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool coInit = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

    INetworkListManager* pNLM = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_NetworkListManager_Local, NULL, CLSCTX_ALL, IID_INetworkListManager_Local, (LPVOID*)&pNLM))) {
        
        // 方法 2：严格互联网连接测试 (系统托盘地球图标通常基于此)
        VARIANT_BOOL isConnectedToInternet;
        // [Fix] MinGW 环境下方法名可能是 IsConnectedToInternet 而非 get_IsConnectedToInternet
        if (SUCCEEDED(pNLM->IsConnectedToInternet(&isConnectedToInternet))) {
            std::cout << "  -> 严格互联网状态 (IsConnectedToInternet): " 
                      << (isConnectedToInternet == VARIANT_TRUE ? "TRUE" : "FALSE") << "\n";
        } else {
            std::cout << "  -> IsConnectedToInternet 调用失败\n";
        }

        // 方法 3：基础连接标志测试 (包含局域网和互联网)
        NLM_CONNECTIVITY connectivity;
        if (SUCCEEDED(pNLM->GetConnectivity(&connectivity))) {
            std::cout << "  -> Connectivity 标志 (Hex): 0x" << std::hex << connectivity << std::dec << "\n";
            std::cout << "     包含 IPv4 互联网: " << ((connectivity & NLM_CONNECTIVITY_IPV4_INTERNET) ? "YES" : "NO") << "\n";
            std::cout << "     包含 IPv4 局域网: " << ((connectivity & NLM_CONNECTIVITY_IPV4_LOCALNETWORK) ? "YES" : "NO") << "\n";
        }

        // 附加测试：遍历当前系统认为激活的连接
        std::cout << "\n[底层详情] 当前 Windows 认为已连接的网络:\n";
        IEnumNetworkConnections* pEnum = nullptr;
        if (SUCCEEDED(pNLM->GetNetworkConnections(&pEnum))) {
            INetworkConnection* pConn = nullptr;
            ULONG fetched = 0;
            while (SUCCEEDED(pEnum->Next(1, &pConn, &fetched)) && fetched > 0) {
                INetwork* pNet = nullptr;
                if (SUCCEEDED(pConn->GetNetwork(&pNet))) {
                    BSTR name;
                    pNet->GetName(&name);
                    NLM_CONNECTIVITY connFlag;
                    pConn->GetConnectivity(&connFlag);
                    
                    // 使用 printf %ls 打印宽字符 BSTR
                    printf("  - 适配器/网络: %ls | 标志: 0x%X\n", (name ? name : L"Unknown"), connFlag);
                    
                    if (name) SysFreeString(name);
                    pNet->Release();
                }
                pConn->Release();
            }
            pEnum->Release();
        }
        pNLM->Release();
    } else {
        std::cout << "  -> COM 接口调用失败\n";
    }

    if (coInit) CoUninitialize();
    std::cout << "========================================\n\n";
}

int main() {
    SetConsoleOutputCP(65001); // CP_UTF8
    
    std::cout << "Windows 网络状态检测工具启动...\n";
    std::cout << "请分别在【开启 Wi-Fi】和【关闭 Wi-Fi(显示地球图标)】时观察输出结果。\n\n";

    while (true) {
        PrintNetworkStatus();
        Sleep(3000); // 每3秒刷新一次
    }

    return 0;
}