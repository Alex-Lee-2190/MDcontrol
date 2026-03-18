#ifndef WIN_CLIPBOARD_H
#define WIN_CLIPBOARD_H

#include "Interfaces.h"
#include <windows.h>
#include <thread>
#include <atomic>
#include <vector>
#include <string>

class WinClipboard : public IClipboard {
public:
    WinClipboard();
    virtual ~WinClipboard();

    void Init(std::function<void(std::string)> onTextChange, std::function<void(const std::vector<std::string>&)> onFilesChange) override;
    void SetLocalClipboard(const std::string& text) override;
    void SetLocalFiles(const std::vector<std::string>& filePaths) override;
    void StartMonitor() override;

    void OnClipboardUpdate();
    void OnSetClipboardMsg(std::string* text);

private:
    void MonitorThreadFunc();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd;
    std::function<void(std::string)> m_text_callback;
    std::function<void(const std::vector<std::string>&)> m_files_callback;
    std::atomic<bool> m_running;
};

#endif