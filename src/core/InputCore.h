#ifndef INPUT_CORE_H
#define INPUT_CORE_H

class InputCore {
public:
    enum MouseEventType {
        Move,
        LeftDown, LeftUp,
        RightDown, RightUp,
        MiddleDown, MiddleUp,
        XButtonDown, XButtonUp,
        Wheel,
        Other
    };

    // Process mouse events, return true if handled
    bool ProcessMouse(int x, int y, MouseEventType type, int data = 0);
    
    // Process keyboard events, return true if handled
    bool ProcessKey(int vk, int scan, bool down, bool sys);
private:
    bool m_ctrlDown = false;
    bool m_altDown = false;
    bool m_shiftDown = false;
    bool m_keyStates[256] = {false};
};

#endif