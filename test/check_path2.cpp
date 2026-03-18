#include <windows.h>
#include <shlobj.h>
#include <exdisp.h>
#include <shlwapi.h>
#include <comdef.h>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shlwapi.lib")

// ----------------------------------------------------------------------------------
// 以下函数与您项目中的 WinSystemUtils.cpp 的实现字节级完全一致
// ----------------------------------------------------------------------------------
std::string GetExplorerPath() {
    std::string path = "";
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    
    IShellWindows* psw;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellWindows, NULL, CLSCTX_ALL, IID_IShellWindows, (void**)&psw))) {
        HWND hwndForeground = GetForegroundWindow();
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
                        // 方法1: 尝试通过 ServiceProvider 获取当前视图路径 (原有逻辑)
                        IServiceProvider* psp;
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

                        // 方法2: 兜底逻辑 (类似 Python 脚本的 LocationURL 方式)
                        // 如果方法1失效，尝试从 LocationURL 解析路径
                        if (path.empty()) {
                            BSTR bstrURL = NULL;
                            if (SUCCEEDED(pwba->get_LocationURL(&bstrURL)) && bstrURL) {
                                std::wstring url(bstrURL, SysStringLen(bstrURL));
                                SysFreeString(bstrURL);
                                
                                // URL 通常以 file:/// 开头
                                if (url.find(L"file:///") == 0) {
                                    // 移除协议头
                                    url = url.substr(8);
                                    // 规范化斜杠
                                    for (auto& c : url) if (c == L'/') c = L'\\';
                                    
                                    // 解码 URL (处理空格 %20 等)
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

int main() {
    // 设置控制台输出为 UTF-8
    SetConsoleOutputCP(CP_UTF8);

    std::cout << "C++ 资源管理器路径检测脚本" << std::endl;
    std::cout << "---------------------------------" << std::endl;
    std::cout << "请将鼠标焦点置于一个资源管理器窗口上，观察下方输出..." << std::endl << std::endl;

    while (true) {
        std::string currentPath = GetExplorerPath();
        if (!currentPath.empty()) {
            std::cout << "当前路径: " << currentPath << "          \r";
        } else {
            std::cout << "未找到激活的资源管理器路径...                  \r";
        }
        Sleep(500);
    }

    return 0;
}