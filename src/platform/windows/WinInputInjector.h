#ifndef WIN_INPUT_INJECTOR_H
#define WIN_INPUT_INJECTOR_H

#include "Interfaces.h"

class WinInputInjector : public IInputInjector {
public:
    void SendMouseClick(int btn, bool down) override;
    void SendMouseScroll(int delta) override;
    void SendKey(int vk, int scan, bool down) override;
};

#endif