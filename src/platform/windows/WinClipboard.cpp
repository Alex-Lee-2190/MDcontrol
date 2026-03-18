#include "Common.h"
#include "WinClipboard.h"
#include "KvmContext.h"
#include <ShlObj.h>
#include <vector>

#define WM_SET_CLIPBOARD (WM_USER + 1)

WinClipboard::WinClipboard() : m_hwnd(NULL), m_running(false) {}
WinClipboard::~WinClipboard() {
    m_running = false;
    if (m_hwnd) PostMessage(m_hwnd, WM_QUIT, 0, 0);
}

void WinClipboard::Init(std::function<void(std::string)> onTextChange, std::function<void(const std::vector<std::string>&)> onFilesChange) {
    m_text_callback = onTextChange;
    m_files_callback = onFilesChange;
}

void WinClipboard::SetLocalClipboard(const std::string& text_utf8) {
    std::string* pText = new std::string(text_utf8);
    if (m_hwnd) PostMessage(m_hwnd, WM_SET_CLIPBOARD, 0, (LPARAM)pText);
}

void WinClipboard::SetLocalFiles(const std::vector<std::string>& filePaths) {
    if (filePaths.empty()) return;

    std::vector<wchar_t> pathBuffer;
    for (const auto& path : filePaths) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, NULL, 0);
        std::vector<wchar_t> wpath(wlen);
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
        pathBuffer.insert(pathBuffer.end(), wpath.begin(), wpath.end());
    }
    pathBuffer.push_back(0); 

    size_t totalSize = sizeof(DROPFILES) + pathBuffer.size() * sizeof(wchar_t);
    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, totalSize);
    if (!hGlob) return;

    DROPFILES* df = (DROPFILES*)GlobalLock(hGlob);
    if (!df) {
        GlobalFree(hGlob);
        return;
    }
    df->pFiles = sizeof(DROPFILES);
    df->pt.x = 0;
    df->pt.y = 0;
    df->fNC = FALSE;
    df->fWide = TRUE; 

    memcpy((char*)df + sizeof(DROPFILES), pathBuffer.data(), pathBuffer.size() * sizeof(wchar_t));
    GlobalUnlock(hGlob);

    g_IgnoreClipUpdate = true;
    if (OpenClipboard(m_hwnd)) {
        EmptyClipboard();
        SetClipboardData(CF_HDROP, hGlob);
        CloseClipboard();
    } else {
        GlobalFree(hGlob);
    }
    g_IgnoreClipUpdate = false;
}


void WinClipboard::StartMonitor() {
    if (m_running) return;
    m_running = true;
    std::thread(&WinClipboard::MonitorThreadFunc, this).detach();
}

void WinClipboard::OnClipboardUpdate() {
    if (g_IgnoreClipUpdate) return;

    for (int i = 0; i < 10; ++i) { 
        if (OpenClipboard(m_hwnd)) {
            if (IsClipboardFormatAvailable(CF_HDROP)) {
                HANDLE hData = GetClipboardData(CF_HDROP);
                if (hData) {
                    HDROP hDrop = (HDROP)GlobalLock(hData);
                    if (hDrop) {
                        UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
                        std::vector<std::string> filePaths;
                        for (UINT j = 0; j < fileCount; ++j) {
                            UINT pathLen = DragQueryFileW(hDrop, j, NULL, 0);
                            std::vector<wchar_t> wpath(pathLen + 1);
                            DragQueryFileW(hDrop, j, wpath.data(), pathLen + 1);
                            
                            int ulen = WideCharToMultiByte(CP_UTF8, 0, wpath.data(), -1, NULL, 0, NULL, NULL);
                            std::string utf8_path(ulen - 1, '\0');
                            WideCharToMultiByte(CP_UTF8, 0, wpath.data(), -1, &utf8_path[0], ulen, NULL, NULL);
                            filePaths.push_back(utf8_path);
                        }
                        GlobalUnlock(hData);
                        if (m_files_callback && !filePaths.empty()) m_files_callback(filePaths);
                    }
                }
            }
            else if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
                HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                if (hData) {
                    wchar_t* wstr = (wchar_t*)GlobalLock(hData);
                    if (wstr) {
                        int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
                        std::string utf8_str(len - 1, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &utf8_str[0], len, NULL, NULL);
                        
                        if (!utf8_str.empty() && utf8_str != g_LastClipText) {
                            g_LastClipText = utf8_str;
                            if (m_text_callback) m_text_callback(utf8_str);
                        }
                        GlobalUnlock(hData);
                    }
                }
            }
            CloseClipboard();
            break;
        }
        Sleep(10);
    }
}

void WinClipboard::OnSetClipboardMsg(std::string* text) {
    if (!text) return;
    
    if (*text != g_LastClipText) {
        g_LastClipText = *text;
        g_IgnoreClipUpdate = true;
        
        if (OpenClipboard(m_hwnd)) {
            EmptyClipboard();
            int wlen = MultiByteToWideChar(CP_UTF8, 0, text->c_str(), -1, NULL, 0);
            HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(wchar_t));
            if (hGlob) {
                wchar_t* wstr = (wchar_t*)GlobalLock(hGlob);
                MultiByteToWideChar(CP_UTF8, 0, text->c_str(), -1, wstr, wlen);
                GlobalUnlock(hGlob);
                SetClipboardData(CF_UNICODETEXT, hGlob);
            }
            CloseClipboard();
        }
        
        g_IgnoreClipUpdate = false;
    }
    delete text;
}

LRESULT CALLBACK WinClipboard::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    WinClipboard* self = dynamic_cast<WinClipboard*>(g_Context->Clipboard);

    switch (uMsg) {
        case WM_CLIPBOARDUPDATE:
            if (self) self->OnClipboardUpdate();
            return 0;
        
        case WM_SET_CLIPBOARD:
            if (self) self->OnSetClipboardMsg((std::string*)lParam);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void WinClipboard::MonitorThreadFunc() {
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WinClipboard::WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"SlaveClipboardMonitor";
    RegisterClassW(&wc);

    m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"SlaveClipboardMonitor", NULL, WS_POPUP, 0, 0, 0, 0, NULL, NULL, GetModuleHandle(NULL), NULL);
    
    if (m_hwnd) AddClipboardFormatListener(m_hwnd);

    MSG msg;
    while (m_running && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (m_hwnd) RemoveClipboardFormatListener(m_hwnd);
}