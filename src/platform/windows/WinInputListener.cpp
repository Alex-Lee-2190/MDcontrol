#include "WinInputListener.h"
#include "Common.h"
#include "SystemUtils.h"
#include "InputCore.h"
#include "KvmContext.h"
#include <windows.h>

static HHOOK s_hMouseHook = NULL;
static HHOOK s_hKbdHook = NULL;

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        MSLLHOOKSTRUCT* p = (MSLLHOOKSTRUCT*)lParam;
        if (p->flags & LLMHF_INJECTED) return CallNextHookEx(s_hMouseHook, nCode, wParam, lParam);

        int x = p->pt.x, y = p->pt.y;
        
        InputCore::MouseEventType type = InputCore::Move;
        int mouseData = 0;
        
        if (wParam == WM_LBUTTONDOWN) type = InputCore::LeftDown;
        else if (wParam == WM_LBUTTONUP) type = InputCore::LeftUp;
        else if (wParam == WM_RBUTTONDOWN) type = InputCore::RightDown;
        else if (wParam == WM_RBUTTONUP) type = InputCore::RightUp;
        else if (wParam == WM_MBUTTONDOWN) type = InputCore::MiddleDown; 
        else if (wParam == WM_MBUTTONUP) type = InputCore::MiddleUp;     
        else if (wParam == WM_XBUTTONDOWN) {
            type = InputCore::XButtonDown; 
            mouseData = HIWORD(p->mouseData); 
        }
        else if (wParam == WM_XBUTTONUP) {
            type = InputCore::XButtonUp;   
            mouseData = HIWORD(p->mouseData);
        }
        else if (wParam == WM_MOUSEWHEEL) {
            type = InputCore::Wheel;
            mouseData = (short)HIWORD(p->mouseData); 
        }

        if (g_Context->InputCore && g_Context->InputCore->ProcessMouse(x, y, type, mouseData)) {
            return 1;
        }
    }
    return CallNextHookEx(s_hMouseHook, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;
        bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        bool sys = (wParam == WM_SYSKEYDOWN || wParam == WM_SYSKEYUP);
        
        if (g_Context->InputCore && g_Context->InputCore->ProcessKey(p->vkCode, p->scanCode, down, sys)) {
            return 1;
        }
    }
    return CallNextHookEx(s_hKbdHook, nCode, wParam, lParam);
}

void WinInputListener::Start() {
    if (!s_hMouseHook) s_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandle(NULL), 0);
    if (!s_hKbdHook) s_hKbdHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
}

void WinInputListener::Stop() {
    if (s_hMouseHook) { UnhookWindowsHookEx(s_hMouseHook); s_hMouseHook = NULL; }
    if (s_hKbdHook) { UnhookWindowsHookEx(s_hKbdHook); s_hKbdHook = NULL; }
}