#ifndef MDCONTROL_CONTEXT_H
#define MDCONTROL_CONTEXT_H

#include "Interfaces.h"
#include "InputCore.h"

// Manage lifecycle of core interfaces and logic objects
class MDControlContext {
public:
    MDControlContext();
    ~MDControlContext();

    // Platform interfaces
    IInputInjector* InputInjector;
    IClipboard*     Clipboard;
    IInputListener* InputListener;
    IBluetoothMgr*  BluetoothMgr;
    IFileLockMgr*   FileLockMgr; 
    ICryptoMgr*     CryptoMgr;   

    // Core logic
    InputCore*      InputCore;
};

#endif