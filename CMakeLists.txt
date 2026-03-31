cmake_minimum_required(VERSION 3.16)

project(MDControl LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Gui Widgets Svg)

# Include paths
include_directories(
    src/core
    src/ui
    src/network
)

# Platform-independent source files
set(SOURCES
    src/main.cpp
    src/core/InputCore.cpp
    src/core/KvmContext.cpp
    src/ui/MainWindow.cpp
    src/ui/MapWidget.cpp
    src/ui/FileTransferWidget.cpp
    src/ui/SpeedGraphWidget.cpp
    src/ui/IconDrawer.cpp
    src/ui/Overlay.cpp
    src/ui/Translation.cpp
    src/network/Master/MasterInput.cpp
    src/network/Master/MasterClipboard.cpp
    src/network/Master/MasterFileTransfer.cpp
    src/network/Master/MasterConnection.cpp
    src/network/Slave/SlaveClipboard.cpp
    src/network/Slave/SlaveFileTransfer.cpp
    src/network/Slave/SlaveConnection.cpp
)

# Platform-specific source files and settings
if(WIN32)
    include_directories(src/platform/windows)
    
    set(APP_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")
    file(WRITE "${APP_ICON_RC}" "IDI_ICON1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/app.ico\"\n")

    list(APPEND SOURCES
        src/platform/windows/WinSystemUtils.cpp
        src/platform/windows/WinPlatform.cpp
        src/platform/windows/WinInputInjector.cpp
        src/platform/windows/WinClipboard.cpp
        src/platform/windows/WinInputListener.cpp
        src/platform/windows/WinBluetooth.cpp
        src/platform/windows/WinFileLockMgr.cpp
        src/platform/windows/WinCryptoMgr.cpp
        ${APP_ICON_RC}
    )
endif()

add_executable(MDControl ${SOURCES})

# Link libraries
target_link_libraries(MDControl PRIVATE Qt6::Gui Qt6::Widgets Qt6::Svg)

if(WIN32)
    target_link_libraries(MDControl PRIVATE
        ws2_32
        winmm
        Bthprops
        uuid
        user32
        gdi32
        iphlpapi
        shlwapi
        ole32
        oleaut32
        advapi32
        crypt32
        bcrypt
    )
    set_target_properties(MDControl PROPERTIES WIN32_EXECUTABLE ON)
endif()

# Independent hash sampling test program
add_executable(hash_test test/hash_test.cpp)
if(WIN32)
    target_link_libraries(hash_test PRIVATE ws2_32)
    set_target_properties(hash_test PROPERTIES WIN32_EXECUTABLE ON)
endif()