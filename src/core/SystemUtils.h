#ifndef SYSTEM_UTILS_H
#define SYSTEM_UTILS_H

#include <cstdint>
#include <vector>
#include <string>

#include "SocketCompat.h"

namespace SystemUtils {
    // Get main screen size
    void GetScreenSize(int& width, int& height);

    // Set cursor position (global coordinates)
    void SetCursorPos(int x, int y);

    // Get cursor position (global coordinates)
    void GetCursorPos(int& x, int& y);

    // Set process to high priority
    void SetProcessHighPriority();

    // Initialize DPI awareness
    void InitDPI();

    // Get current system DPI scale
    double GetDPIScale();

    // Get system UI language ("zh", "en", etc.)
    std::string GetSystemLanguage();

    // Get system time in milliseconds
    uint32_t GetTimeMS();

    // Begin high precision timer
    void BeginHighPrecisionTimer();

    // End high precision timer
    void EndHighPrecisionTimer();

    // Show error message box
    void ShowErrorMessage(const char* title, const char* message);

    // Get all local non-loopback IPv4 addresses
    std::vector<std::string> GetLocalIPAddresses();

    // Get current active explorer path
    std::string GetCurrentExplorerPath();

    // Get computer name
    std::string GetComputerNameStr();

    // Get system and hardware properties
    std::string GetSystemProperties();

    // Check OS-level network connectivity
    bool HasNetworkConnectivity();
    
    // Get active network name
    std::string GetActiveNetworkName();

    // Check if socket is Bluetooth (abstract)
    bool IsBluetoothSocket(SocketHandle sock);

    // Get peer address (abstract)
    std::string GetPeerAddress(SocketHandle sock);

    // Write log
    void WriteLog(const std::string& msg);

    // Open log directory
    void OpenLogDirectory();

    // UTF-8 to UTF-16 conversion
    std::wstring Utf8ToWString(const std::string& str);
    
    // Get application directory
    std::string GetAppDir();
    
    // Launch hash test process
    void LaunchHashTest(const std::string& targetFile);
}

#endif // SYSTEM_UTILS_H