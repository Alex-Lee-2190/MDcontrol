#include "KvmContext.h"

MDControlContext::MDControlContext() {
    InputInjector = nullptr;
    Clipboard = nullptr;
    InputListener = nullptr;
    BluetoothMgr = nullptr;
    FileLockMgr = nullptr;
    CryptoMgr = nullptr;
    LanDiscoveryMgr = nullptr;
    InputCore = nullptr;
}

MDControlContext::~MDControlContext() {
    delete InputInjector;
    delete Clipboard;
    delete InputListener;
    delete BluetoothMgr;
    delete FileLockMgr;
    delete CryptoMgr;
    delete LanDiscoveryMgr;
    delete InputCore;
}