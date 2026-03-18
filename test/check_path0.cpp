#include <windows.h>
#include <shlobj.h>
#include <exdisp.h>
#include <shlwapi.h>
#include <comdef.h>
#include <iostream>
#include <string>
#include <vector>
#include <map>

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
        std::cout << "Foreground HWND: " << (void*)hwndForeground << std::endl;

        // 1. 获取主窗口中心点 (用于物理命中测试)
        RECT rcFg;
        GetWindowRect(hwndForeground, &rcFg);
        POINT ptCenter = { (rcFg.left + rcFg.right) / 2, (rcFg.top + rcFg.bottom) / 2 };
        std::cout << "Window Rect: [" << rcFg.left << "," << rcFg.top << " - " << rcFg.right << "," << rcFg.bottom << "]" << std::endl;
        std::cout << "Center Point: (" << ptCenter.x << ", " << ptCenter.y << ")" << std::endl;

        // 获取中心点最顶层的窗口句柄 (这是判断 Z 序最有效的方法)
        HWND hHit = WindowFromPoint(ptCenter);
        std::cout << "Hit Window at Center: " << (void*)hHit << std::endl;

        // 2. 建立 ViewHWND -> Path 的映射表，并记录所有候选者
        struct TabInfo {
            HWND hView;
            std::string path;
            RECT rcView;
        };
        std::vector<TabInfo> tabs;

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

                    if (hwnd == hwndForeground) {
                        TabInfo info = {0};
                        
                        // 获取 View 句柄
                        IServiceProvider* psp = nullptr;
                        if (SUCCEEDED(pwba->QueryInterface(IID_IServiceProvider, (void**)&psp))) {
                            IShellBrowser* psb = nullptr;
                            if (SUCCEEDED(psp->QueryService(SID_STopLevelBrowser, IID_IShellBrowser, (void**)&psb))) {
                                IShellView* psv = nullptr;
                                if (SUCCEEDED(psb->QueryActiveShellView(&psv))) {
                                    psv->GetWindow(&info.hView);
                                    if (info.hView) {
                                        GetWindowRect(info.hView, &info.rcView);
                                    }
                                    
                                    // 提取路径
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
                                                        info.path.resize(size - 1);
                                                        WideCharToMultiByte(CP_UTF8, 0, wpath, -1, &info.path[0], size, NULL, NULL);
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

                        // 如果路径为空，尝试 LocationURL
                        if (info.path.empty()) {
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
                                            info.path.resize(size - 1);
                                            WideCharToMultiByte(CP_UTF8, 0, buf, -1, &info.path[0], size, NULL, NULL);
                                        }
                                    }
                                }
                            }
                        }

                        if (!info.path.empty()) {
                            tabs.push_back(info);
                            std::cout << "  [Candidate] ViewHWND: " << (void*)info.hView 
                                      << " | Rect: " << info.rcView.left << "," << info.rcView.top 
                                      << " | Path: " << info.path << std::endl;
                        }
                    }
                    pwba->Release();
                }
                pdisp->Release();
            }
        }
        psw->Release();

        // 3. 核心判定：检查 hHit 是否属于某个 Candidate 的子窗口/自身
        for (const auto& tab : tabs) {
            // 方法A: 检查命中窗口是否是 View 窗口的后代
            bool isMatch = false;
            if (hHit == tab.hView) isMatch = true;
            else if (IsChild(tab.hView, hHit)) isMatch = true;
            
            // 方法B: 某些情况 WindowFromPoint 可能只命中父容器，检查包含关系
            // 如果 hHit 是 hView 的祖先，且 hView 的 Rect 包含了中心点？ (不太可靠，Z序是关键)
            
            // 回溯法：从命中窗口向上找，看是否遇到 Tab 的 hView
            HWND hWalk = hHit;
            while (hWalk && !isMatch && hWalk != hwndForeground) {
                if (hWalk == tab.hView) isMatch = true;
                hWalk = GetParent(hWalk);
            }

            if (isMatch) {
                std::cout << ">>> 命中匹配! Active Tab Path: " << tab.path << std::endl;
                finalPath = tab.path;
                break;
            }
        }
    }
    CoUninitialize();
    return finalPath;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    std::cout << "C++ 资源管理器路径检测 - 几何命中测试模式" << std::endl;
    std::cout << "---------------------------------" << std::endl;
    std::cout << "原理：检测主窗口中心点的最顶层窗口归属。" << std::endl << std::endl;

    while (true) {
        std::string currentPath = GetExplorerPath();
        if (!currentPath.empty()) {
            std::cout << ">>> [RESULT] 最终路径: " << currentPath << std::endl;
        } else {
            std::cout << ">>> [RESULT] 未能锁定激活标签页 (可能鼠标遮挡或最小化)" << std::endl;
        }
        Sleep(1000); 
    }

    return 0;
}