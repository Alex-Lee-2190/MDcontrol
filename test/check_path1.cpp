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

std::string GetExplorerPath() {
    std::string finalPath = "";
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    
    IShellWindows* psw;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellWindows, NULL, CLSCTX_ALL, IID_IShellWindows, (void**)&psw))) {
        HWND hwndForeground = GetForegroundWindow();
        long count = 0;
        psw->get_Count(&count);
        
        std::cout << "\n---------------- [DEBUG DIAGNOSIS] ----------------" << std::endl;
        std::cout << "Foreground Window HWND: " << (void*)hwndForeground << " | Total ShellWindows: " << count << std::endl;

        for (long i = 0; i < count; ++i) {
            VARIANT v;
            V_VT(&v) = VT_I4;
            V_I4(&v) = i;
            IDispatch* pdisp;
            if (SUCCEEDED(psw->Item(v, &pdisp))) {
                IWebBrowserApp* pwba;
                if (SUCCEEDED(pdisp->QueryInterface(IID_IWebBrowserApp, (void**)&pwba))) {
                    HWND hwnd = NULL;
                    pwba->get_HWND((SHANDLE_PTR*)&hwnd);

                    // 仅当窗口句柄匹配前台窗口时才进行深入检查（这就是多标签页的情况）
                    if (hwnd == hwndForeground) {
                        std::string currentPath = "Unknown";
                        bool isVisible = false;
                        HWND hView = NULL;

                        // 1. 尝试获取该标签页的路径
                        IServiceProvider* psp = nullptr;
                        if (SUCCEEDED(pwba->QueryInterface(IID_IServiceProvider, (void**)&psp))) {
                            IShellBrowser* psb = nullptr;
                            if (SUCCEEDED(psp->QueryService(SID_STopLevelBrowser, IID_IShellBrowser, (void**)&psb))) {
                                IShellView* psv = nullptr;
                                if (SUCCEEDED(psb->QueryActiveShellView(&psv))) {
                                    // 2. 获取视图句柄并检查可见性
                                    if (SUCCEEDED(psv->GetWindow(&hView))) {
                                        isVisible = IsWindowVisible(hView); 
                                    }

                                    // 3. 提取路径
                                    IFolderView* pfv = nullptr;
                                    if (SUCCEEDED(psv->QueryInterface(IID_IFolderView, (void**)&pfv))) {
                                        IPersistFolder2* ppf2 = nullptr;
                                        if (SUCCEEDED(pfv->GetFolder(IID_IPersistFolder2, (void**)&ppf2))) {
                                            LPITEMIDLIST pidl = nullptr;
                                            if (SUCCEEDED(ppf2->GetCurFolder(&pidl))) {
                                                wchar_t wpath[MAX_PATH];
                                                if (SHGetPathFromIDListW(pidl, wpath)) {
                                                    int size = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, NULL, 0, NULL, NULL);
                                                    if (size > 0) {
                                                        currentPath.resize(size - 1);
                                                        WideCharToMultiByte(CP_UTF8, 0, wpath, -1, &currentPath[0], size, NULL, NULL);
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

                        // 如果 COM 提取路径失败，尝试 LocationURL 兜底
                        if (currentPath == "Unknown") {
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
                                            currentPath.resize(size - 1);
                                            WideCharToMultiByte(CP_UTF8, 0, buf, -1, &currentPath[0], size, NULL, NULL);
                                        }
                                    }
                                }
                            }
                        }

                        // 打印详细诊断信息
                        std::cout << "  [TAB " << i << "] ViewHWND: " << (void*)hView 
                                  << " | Visible: " << (isVisible ? "TRUE" : "FALSE") 
                                  << " | Path: " << currentPath << std::endl;

                        // 逻辑：优先选择可见的标签页作为最终结果
                        if (isVisible) {
                            finalPath = currentPath;
                        } else if (finalPath.empty() && currentPath != "Unknown") {
                            // 如果没有可见标签（异常情况），暂时记录第一个找到的路径
                            finalPath = currentPath;
                        }
                    }
                    pwba->Release();
                }
                pdisp->Release();
            }
        }
        psw->Release();
    }
    CoUninitialize();
    return finalPath;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    std::cout << "C++ 资源管理器路径检测 - 诊断模式" << std::endl;
    std::cout << "---------------------------------" << std::endl;
    std::cout << "请切换 Explorer 标签页，观察下方输出可见性(Visible)的变化..." << std::endl << std::endl;

    while (true) {
        std::string currentPath = GetExplorerPath();
        if (!currentPath.empty()) {
            std::cout << ">>> 最终判定路径: " << currentPath << std::endl;
        } else {
            std::cout << ">>> 未找到路径..." << std::endl;
        }
        Sleep(1000); // 增加间隔以免刷屏太快无法阅读
    }

    return 0;
}