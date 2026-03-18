#include "WinInputInjector.h"
#include <windows.h>

void WinInputInjector::SendMouseClick(int btn, bool down) {
    INPUT input = { 0 }; input.type = INPUT_MOUSE;
    if (btn == 1) input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    else if (btn == 2) input.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    else if (btn == 3) input.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; 
    else if (btn == 4) { 
        input.mi.mouseData = XBUTTON1;
        input.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
    }
    else if (btn == 5) { 
        input.mi.mouseData = XBUTTON2;
        input.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
    }
    SendInput(1, &input, sizeof(INPUT));
}

void WinInputInjector::SendMouseScroll(int delta) {
    INPUT input = { 0 }; input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL; input.mi.mouseData = delta * WHEEL_DELTA;
    SendInput(1, &input, sizeof(INPUT));
}

void WinInputInjector::SendKey(int vk, int scan, bool down) {
    INPUT input = { 0 }; input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk; input.ki.wScan = scan; input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}