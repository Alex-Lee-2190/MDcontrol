#include "SystemUtils.h"
#include "Common.h" 
#include <windows.h>
#include <cstdio>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <shlobj.h>
#include <exdisp.h>
#include <shlwapi.h>
#include <comdef.h>
#include <netlistmgr.h>
#include <ws2bth.h>
#include <shellapi.h>
#include <QtGui/QGuiApplication>
#include <QtGui/QStyleHints>
#include <QtWidgets/QApplication>
#include <QtGui/QPalette>
#include <QtWidgets/QStyle>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

#include <QtCore/QEvent>
#include <QtGui/QWindow>

typedef HRESULT(WINAPI *DwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);

static void SetDarkTitleBar(HWND hwnd, bool dark) {
    if (!hwnd) return;
    static HMODULE hDwm = LoadLibraryA("dwmapi.dll");
    if (hDwm) {
        static DwmSetWindowAttribute_t pDwmSetWindowAttribute = (DwmSetWindowAttribute_t)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (pDwmSetWindowAttribute) {
            BOOL bDark = dark ? TRUE : FALSE;
            // 19 兼容旧版 Windows 10，20 兼容新版 Windows 10 和 Windows 11
            pDwmSetWindowAttribute(hwnd, 19, &bDark, sizeof(bDark));
            pDwmSetWindowAttribute(hwnd, 20, &bDark, sizeof(bDark));
        }
    }
}

// 获取 Windows 构建版本号，Win11 起始为 22000
static DWORD GetWinBuildNumber() {
    DWORD build = 0;
    HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
    if (hMod) {
        typedef struct _RTL_OSVERSIONINFOW_LOCAL {
            DWORD dwOSVersionInfoSize;
            DWORD dwMajorVersion;
            DWORD dwMinorVersion;
            DWORD dwBuildNumber;
            DWORD dwPlatformId;
            WCHAR szCSDVersion[128];
        } RTL_OSVERSIONINFOW_LOCAL;
        typedef NTSTATUS(WINAPI *RtlGetVersionPtr_Local)(RTL_OSVERSIONINFOW_LOCAL*);
        RtlGetVersionPtr_Local pRtlGetVersion = (RtlGetVersionPtr_Local)GetProcAddress(hMod, "RtlGetVersion");
        if (pRtlGetVersion) {
            RTL_OSVERSIONINFOW_LOCAL rovi = {0};
            rovi.dwOSVersionInfoSize = sizeof(rovi);
            if (pRtlGetVersion(&rovi) == 0) {
                build = rovi.dwBuildNumber;
            }
        }
    }
    return build;
}

class ThemeEventFilter : public QObject {
public:
    bool dark = false;

    static ThemeEventFilter* instance() {
        static ThemeEventFilter* s_inst = nullptr;
        if (!s_inst && QCoreApplication::instance()) {
            s_inst = new ThemeEventFilter();
            QCoreApplication::instance()->installEventFilter(s_inst);
        }
        return s_inst;
    }

    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::Show) {
            if (obj->isWindowType() || obj->isWidgetType()) {
                for (QWindow* win : QGuiApplication::topLevelWindows()) {
                    SetDarkTitleBar((HWND)win->winId(), dark);
                }
            }
        }
        return false;
    }
};

namespace SystemUtils {

    const CLSID CLSID_NetworkListManager_Local = {0xDCB00C01, 0x570F, 0x4A9B, {0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B}};
    const IID IID_INetworkListManager_Local = {0xDCB00000, 0x570F, 0x4A9B, {0x8D, 0x69, 0x19, 0x9F, 0xDB, 0xA5, 0x72, 0x3B}};

    std::string GetSystemLanguage() {
        LANGID langId = GetUserDefaultUILanguage();
        if (PRIMARYLANGID(langId) == LANG_CHINESE) return "zh";
        return "en";
    }

    uint32_t GetCurrentThreadId() {
        return ::GetCurrentThreadId();
    }

    bool HasNetworkConnectivity() {
        bool hasNet = false;
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        bool coInit = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
        
        INetworkListManager* pNLM = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_NetworkListManager_Local, NULL, CLSCTX_ALL, IID_INetworkListManager_Local, (LPVOID*)&pNLM))) {
            VARIANT_BOOL isConnected = VARIANT_FALSE;
            if (SUCCEEDED(pNLM->IsConnectedToInternet(&isConnected))) {
                hasNet = (isConnected == VARIANT_TRUE);
            }
            pNLM->Release();
        }
        
        if (coInit) CoUninitialize();
        MDC_LOG_TRACE(LogTag::SYS, "HasNetworkConnectivity returning: %d", hasNet);
        return hasNet;
    }

    std::string GetActiveNetworkName() {
        std::string netName = "";
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        bool coInit = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
        
        INetworkListManager* pNLM = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_NetworkListManager_Local, NULL, CLSCTX_ALL, IID_INetworkListManager_Local, (LPVOID*)&pNLM))) {
            IEnumNetworkConnections* pEnum = nullptr;
            if (SUCCEEDED(pNLM->GetNetworkConnections(&pEnum))) {
                INetworkConnection* pConn = nullptr;
                ULONG fetched = 0;
                while (SUCCEEDED(pEnum->Next(1, &pConn, &fetched)) && fetched > 0) {
                    NLM_CONNECTIVITY connFlag;
                    if (SUCCEEDED(pConn->GetConnectivity(&connFlag))) {
                        if (connFlag & (NLM_CONNECTIVITY_IPV4_INTERNET | NLM_CONNECTIVITY_IPV6_INTERNET)) {
                            INetwork* pNet = nullptr;
                            if (SUCCEEDED(pConn->GetNetwork(&pNet))) {
                                BSTR name;
                                if (SUCCEEDED(pNet->GetName(&name))) {
                                    int len = WideCharToMultiByte(CP_UTF8, 0, name, -1, NULL, 0, NULL, NULL);
                                    if (len > 0) {
                                        netName.resize(len - 1);
                                        WideCharToMultiByte(CP_UTF8, 0, name, -1, &netName[0], len, NULL, NULL);
                                    }
                                    SysFreeString(name);
                                }
                                pNet->Release();
                            }
                            pConn->Release();
                            break; 
                        }
                    }
                    pConn->Release();
                }
                pEnum->Release();
            }
            pNLM->Release();
        }
        
        if (coInit) CoUninitialize();
        MDC_LOG_TRACE(LogTag::SYS, "GetActiveNetworkName returning length: %zu", netName.length());
        return netName;
    }

    void GetScreenSize(int& width, int& height) {
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
    }

    void SetCursorPos(int x, int y) {
        ::SetCursorPos(x, y);
    }

    void GetCursorPos(int& x, int& y) {
        POINT pt;
        if (::GetCursorPos(&pt)) {
            x = pt.x;
            y = pt.y;
        } else {
            x = 0;
            y = 0;
        }
    }

    void SetProcessHighPriority() {
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    }

    void SetThreadBackgroundPriority() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    }

    void InitDPI() {
        SetProcessDPIAware();
    }

    double GetDPIScale() {
        HDC hdc = GetDC(NULL);
        int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(NULL, hdc);
        return dpi / 96.0;
    }

    uint32_t GetTimeMS() {
        return GetTickCount();
    }

    void BeginHighPrecisionTimer() {
        timeBeginPeriod(1);
    }

    void EndHighPrecisionTimer() {
        timeEndPeriod(1);
    }

    void ShowErrorMessage(const char* title, const char* message) {
        MessageBoxA(NULL, message, title, MB_ICONERROR);
    }

    std::vector<std::string> GetLocalIPAddresses() {
        std::vector<std::string> ips;
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            struct addrinfo hints = { 0 };
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;

            struct addrinfo* result = NULL;
            if (getaddrinfo(hostname, NULL, &hints, &result) == 0) {
                for (struct addrinfo* ptr = result; ptr != NULL; ptr = ptr->ai_next) {
                    struct sockaddr_in* sockaddr_ipv4 = (struct sockaddr_in*)ptr->ai_addr;
                    char ipStr[INET_ADDRSTRLEN];
                    if (inet_ntop(AF_INET, &sockaddr_ipv4->sin_addr, ipStr, sizeof(ipStr))) {
                        std::string s(ipStr);
                        if (s.find("127.") != 0) { 
                            ips.push_back(s);
                        }
                    }
                }
                freeaddrinfo(result);
            }
        }
        MDC_LOG_INFO(LogTag::SYS, "GetLocalIPAddresses found %zu addresses", ips.size());
        return ips;
    }

    std::string GetCurrentExplorerPath() {
        std::string path = "";

        HWND hwndForeground = GetForegroundWindow();
        char className[256] = { 0 };
        if (GetClassNameA(hwndForeground, className, sizeof(className))) {
            if (lstrcmpiA(className, "Progman") == 0 || lstrcmpiA(className, "WorkerW") == 0) {
                char desktopPath[MAX_PATH] = { 0 };
                if (SHGetSpecialFolderPathA(NULL, desktopPath, CSIDL_DESKTOPDIRECTORY, FALSE)) {
                    return std::string(desktopPath);
                }
            }
        }

        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        
        IShellWindows* psw;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellWindows, NULL, CLSCTX_ALL, IID_IShellWindows, (void**)&psw))) {
            RECT rcFg;
            GetWindowRect(hwndForeground, &rcFg);
            POINT ptCenter = { (rcFg.left + rcFg.right) / 2, (rcFg.top + rcFg.bottom) / 2 };
            HWND hHit = WindowFromPoint(ptCenter);

            long count = 0;
            psw->get_Count(&count);
            for (long i = 0; i < count; ++i) {
                VARIANT v;
                V_VT(&v) = VT_I4;
                V_I4(&v) = i;
                IDispatch* pdisp;
                if (SUCCEEDED(psw->Item(v, &pdisp))) {
                    IWebBrowserApp* pwba;
                    if (SUCCEEDED(pdisp->QueryInterface(IID_IWebBrowserApp, (void**)&pwba))) {
                        HWND hwnd;
                        pwba->get_HWND((SHANDLE_PTR*)&hwnd);
                        
                        if (hwnd == hwndForeground) {
                            HWND hView = NULL;
                            IServiceProvider* psp = nullptr;
                            
                            if (SUCCEEDED(pwba->QueryInterface(IID_IServiceProvider, (void**)&psp))) {
                                IShellBrowser* psb = nullptr;
                                if (SUCCEEDED(psp->QueryService(SID_STopLevelBrowser, IID_IShellBrowser, (void**)&psb))) {
                                    IShellView* psv = nullptr;
                                    if (SUCCEEDED(psb->QueryActiveShellView(&psv))) {
                                        psv->GetWindow(&hView);
                                        psv->Release();
                                    }
                                    psb->Release();
                                }
                                psp->Release();
                            }

                            bool isMatch = false;
                            if (hView) {
                                if (hHit == hView) isMatch = true;
                                else if (IsChild(hView, hHit)) isMatch = true;
                                else {
                                    HWND hWalk = hHit;
                                    while (hWalk && hWalk != hwndForeground) {
                                        if (hWalk == hView) {
                                            isMatch = true;
                                            break;
                                        }
                                        hWalk = GetParent(hWalk);
                                    }
                                }
                            }

                            if (isMatch) {
                                if (SUCCEEDED(pwba->QueryInterface(IID_IServiceProvider, (void**)&psp))) {
                                    IShellBrowser* psb;
                                    if (SUCCEEDED(psp->QueryService(SID_STopLevelBrowser, IID_IShellBrowser, (void**)&psb))) {
                                        IShellView* psv;
                                        if (SUCCEEDED(psb->QueryActiveShellView(&psv))) {
                                            IFolderView* pfv;
                                            if (SUCCEEDED(psv->QueryInterface(IID_IFolderView, (void**)&pfv))) {
                                                IPersistFolder2* ppf2;
                                                if (SUCCEEDED(pfv->GetFolder(IID_IPersistFolder2, (void**)&ppf2))) {
                                                    LPITEMIDLIST pidl;
                                                    if (SUCCEEDED(ppf2->GetCurFolder(&pidl))) {
                                                        wchar_t wpath[MAX_PATH];
                                                        if (SHGetPathFromIDListW(pidl, wpath)) {
                                                            int size = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, NULL, 0, NULL, NULL);
                                                            if (size > 0) {
                                                                path.resize(size - 1);
                                                                WideCharToMultiByte(CP_UTF8, 0, wpath, -1, &path[0], size, NULL, NULL);
                                                            }
                                                        }
                                                        CoTaskMemFree(pidl);
                                                    }
                                                    ppf2->Release();
                                                }
                                                pfv->Release();
                                            }
                                            psv->Release();
                                        }
                                        psb->Release();
                                    }
                                    psp->Release();
                                }

                                if (path.empty()) {
                                    BSTR bstrURL = NULL;
                                    if (SUCCEEDED(pwba->get_LocationURL(&bstrURL)) && bstrURL) {
                                        std::wstring url(bstrURL, SysStringLen(bstrURL));
                                        SysFreeString(bstrURL);
                                        
                                        if (url.find(L"file:///") == 0) {
                                            url = url.substr(8);
                                            for (auto& c : url) if (c == L'/') c = L'\\';
                                            wchar_t buf[MAX_PATH];
                                            DWORD len = MAX_PATH;
                                            if (S_OK == UrlUnescapeW((PWSTR)url.c_str(), buf, &len, 0)) {
                                                int size = WideCharToMultiByte(CP_UTF8, 0, buf, -1, NULL, 0, NULL, NULL);
                                                if (size > 0) {
                                                    path.resize(size - 1);
                                                    WideCharToMultiByte(CP_UTF8, 0, buf, -1, &path[0], size, NULL, NULL);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        pwba->Release();
                    }
                    pdisp->Release();
                }
                if (!path.empty()) break;
            }
            psw->Release();
        }
        CoUninitialize();
        return path;
    }

    std::string GetComputerNameStr() {
        wchar_t wbuf[256];
        DWORD len = sizeof(wbuf) / sizeof(wbuf[0]);
        if (GetComputerNameW(wbuf, &len)) {
            int ulen = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, NULL, 0, NULL, NULL);
            if (ulen > 0) {
                std::string utf8_str(ulen - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, &utf8_str[0], ulen, NULL, NULL);
                return utf8_str;
            }
        }
        return "Unknown";
    }

    std::string GetSystemProperties() {
        bool isZh = true;
        if (g_Language == 0) isZh = (GetSystemLanguage() == "zh");
        else isZh = (g_Language == 1);

        std::string props;
        props += (isZh ? "设备名称: " : "Device Name: ") + GetComputerNameStr() + "\n";
        
        HMODULE hMod = GetModuleHandleW(L"ntdll.dll");
        if (hMod) {
            typedef struct _RTL_OSVERSIONINFOW {
                DWORD dwOSVersionInfoSize;
                DWORD dwMajorVersion;
                DWORD dwMinorVersion;
                DWORD dwBuildNumber;
                DWORD dwPlatformId;
                WCHAR szCSDVersion[128];
            } RTL_OSVERSIONINFOW;
            typedef NTSTATUS(WINAPI *RtlGetVersionPtr)(RTL_OSVERSIONINFOW*);
            RtlGetVersionPtr pRtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hMod, "RtlGetVersion");
            if (pRtlGetVersion) {
                RTL_OSVERSIONINFOW rovi = {0};
                rovi.dwOSVersionInfoSize = sizeof(rovi);
                if (pRtlGetVersion(&rovi) == 0) {
                    props += (isZh ? "系统版本: Windows NT " : "System Version: Windows NT ") + std::to_string(rovi.dwMajorVersion) + "." + 
                             std::to_string(rovi.dwMinorVersion) + " (Build " + std::to_string(rovi.dwBuildNumber) + ")\n";
                }
            }
        }
        
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            props += (isZh ? "物理内存: " : "Physical Memory: ") + std::to_string((memInfo.ullTotalPhys + (1024ULL * 1024 * 1024 / 2)) / (1024ULL * 1024 * 1024)) + " GB\n";
        }
        return props;
    }

    bool IsBluetoothSocket(SocketHandle sock) {
        if (sock == INVALID_SOCKET_HANDLE) return false;
        struct sockaddr_storage peer;
        int peer_len = sizeof(peer);
        if (getsockname((SOCKET)sock, (struct sockaddr*)&peer, &peer_len) == 0) {
            return peer.ss_family == AF_BTH;
        }
        return false;
    }

    std::string GetPeerAddress(SocketHandle sock) {
        if (sock == INVALID_SOCKET_HANDLE) return "";
        struct sockaddr_storage peer;
        int peer_len = sizeof(peer);
        if (getpeername((SOCKET)sock, (struct sockaddr*)&peer, &peer_len) == 0) {
            if (peer.ss_family == AF_BTH) {
                SOCKADDR_BTH* bth = (SOCKADDR_BTH*)&peer;
                char mac[32];
                sprintf(mac, "%012llX", bth->btAddr);
                return std::string(mac);
            } else if (peer.ss_family == AF_INET) {
                struct sockaddr_in* addr = (struct sockaddr_in*)&peer;
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
                return std::string(ip);
            }
        }
        return IsBluetoothSocket(sock) ? "000000000000" : "127.0.0.1";
    }

    void WriteLog(const std::string& msg) {
        static int s_lineCount = 0;
        static int s_fileIndex = 1;
        static std::string s_startupTime = "";
        static std::string s_logDirPath = "";

        if (s_startupTime.empty()) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            char timeBuf[64];
            sprintf(timeBuf, "%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            s_startupTime = timeBuf;

            char path[MAX_PATH];
            GetModuleFileNameA(NULL, path, MAX_PATH);
            std::string exePath(path);
            size_t pos = exePath.find_last_of("\\/");
            if (pos != std::string::npos) {
                s_logDirPath = exePath.substr(0, pos) + "\\log";
                CreateDirectoryA(s_logDirPath.c_str(), NULL);
            }
        }

        if (s_logDirPath.empty()) return;

        if (s_lineCount >= 50000) {
            s_lineCount = 0;
            s_fileIndex++;
        }

        char fileName[128];
        sprintf(fileName, "\\MDC_%s_%04d.log", s_startupTime.c_str());
        std::string fullPath = s_logDirPath + fileName;

        FILE* f = fopen(fullPath.c_str(), "a");
        if (f) {
            fprintf(f, "%s", msg.c_str());
            fclose(f);
            s_lineCount++;
        }
    }

    void OpenLogDirectory() {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        std::string dir = std::string(path);
        size_t pos = dir.find_last_of("\\/");
        if (pos != std::string::npos) {
            dir = dir.substr(0, pos) + "\\log";
            CreateDirectoryA(dir.c_str(), NULL); 
            ShellExecuteA(NULL, "open", dir.c_str(), NULL, NULL, SW_SHOW);
        }
    }

    std::wstring Utf8ToWString(const std::string& str) {
        if (str.empty()) return std::wstring();
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
        return wstrTo;
    }

    std::string GetAppDir() {
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        std::string path(buffer);
        size_t pos = path.find_last_of("\\/");
        return (pos == std::string::npos) ? "" : path.substr(0, pos);
    }

void LaunchHashTest(const std::string& targetFile) {
        std::string exeDir = GetAppDir();
        std::string exePath = exeDir + "\\hash_test.exe";
        
        if (GetFileAttributesA(exePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            exePath = exeDir + "\\build\\hash_test.exe";
            if (GetFileAttributesA(exePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
                exePath = exeDir + "\\..\\build\\hash_test.exe";
            }
        }
        
        std::string absTargetFile = exeDir + "\\" + targetFile;
        if (GetFileAttributesA(absTargetFile.c_str()) == INVALID_FILE_ATTRIBUTES) {
            absTargetFile = targetFile; 
        }

        std::string cmd = "\"" + exePath + "\" \"" + absTargetFile + "\"";

        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        
        std::vector<char> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back('\0');

        if (CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, FALSE, 0, NULL, exeDir.c_str(), &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }

    void ApplyTheme(int mode) {
        if (!QGuiApplication::instance()) return;

        bool isDark = false;
        if (mode == 1) { // Light
            isDark = false;
        } else if (mode == 2) { // Dark
            isDark = true;
        } else { // Auto
            DWORD value = 1;
            DWORD size = sizeof(value);
            HKEY hKey;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&value, &size);
                RegCloseKey(hKey);
            }
            isDark = (value == 0);
        }

        MDC_LOG_INFO(LogTag::SYS, "ApplyTheme executed: mode=%d, final isDark=%d", mode, isDark);
        
        QGuiApplication::styleHints()->setColorScheme(isDark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);

        if (GetWinBuildNumber() < 22000) {
            if (isDark) {
                QApplication::setStyle("Fusion");
                QPalette darkPalette;
                darkPalette.setColor(QPalette::Window, QColor(45, 45, 48));
                darkPalette.setColor(QPalette::WindowText, Qt::white);
                darkPalette.setColor(QPalette::Base, QColor(30, 30, 30));
                darkPalette.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
                darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
                darkPalette.setColor(QPalette::ToolTipText, Qt::white);
                darkPalette.setColor(QPalette::Text, Qt::white);
                darkPalette.setColor(QPalette::Button, QColor(60, 60, 60));
                darkPalette.setColor(QPalette::ButtonText, Qt::white);
                darkPalette.setColor(QPalette::BrightText, Qt::red);
                darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
                darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
                darkPalette.setColor(QPalette::HighlightedText, Qt::white);
                
                darkPalette.setColor(QPalette::Disabled, QPalette::Text, QColor(127, 127, 127));
                darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(127, 127, 127));
                darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(127, 127, 127));
                
                QApplication::setPalette(darkPalette);
            } else {
                QApplication::setStyle("windowsvista");
                QApplication::setPalette(QPalette());
            }
        }

        ThemeEventFilter* filter = ThemeEventFilter::instance();
        if (filter) {
            filter->dark = isDark;
            for (QWindow* win : QGuiApplication::topLevelWindows()) {
                HWND hwnd = (HWND)win->winId();
                if (hwnd) {
                    SetDarkTitleBar(hwnd, isDark);
                    SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
                }
            }
        }
    }
}