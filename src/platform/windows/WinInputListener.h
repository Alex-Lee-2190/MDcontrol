#ifndef WIN_INPUT_LISTENER_H
#define WIN_INPUT_LISTENER_H

#include "Interfaces.h"

class WinInputListener : public IInputListener {
public:
    void Start() override;
    void Stop() override;
};

#endif