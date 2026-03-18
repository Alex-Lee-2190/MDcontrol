#include "Common.h"
#include "KvmContext.h"
#include "WinInputInjector.h"
#include "WinClipboard.h"
#include "WinInputListener.h"
#include "WinBluetooth.h"
#include "WinFileLockMgr.h"
#include "WinCryptoMgr.h"

void InitPlatform() {
    g_Context->InputInjector = new WinInputInjector();
    g_Context->Clipboard = new WinClipboard();
    g_Context->InputListener = new WinInputListener();
    g_Context->BluetoothMgr = new WinBluetoothMgr();
    g_Context->FileLockMgr = new WinFileLockMgr();
    g_Context->CryptoMgr = new WinCryptoMgr();
}