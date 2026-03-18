cmake_minimum_required(VERSION 3.16)

project(MDControl LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Gui Widgets Svg)

# 包含路径
include_directories(
    src/core
    src/ui
    src/network
)

# 平台无关源文件
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
    src/network/Master/MasterInput.cpp
    src/network/Master/MasterClipboard.cpp
    src/network/Master/MasterFileTransfer.cpp
    src/network/Master/MasterConnection.cpp
    src/network/Slave/SlaveClipboard.cpp
    src/network/Slave/SlaveFileTransfer.cpp
    src/network/Slave/SlaveConnection.cpp
)

# 平台特定源文件和设置
if(WIN32)
    include_directories(src/platform/windows)
    
    # [New] 生成并编译资源文件以设置 exe 的文件图标
    # 要求在项目根目录（CMakeLists.txt同级）存在 app.ico 文件
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
        ${APP_ICON_RC} # [New] 将资源文件加入编译链
    )
endif()

add_executable(MDControl ${SOURCES})

# 链接库
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
    )
    # 针对 Windows 子系统的设置 (避免弹出控制台，如需调试可注释掉)
    set_target_properties(MDControl PROPERTIES WIN32_EXECUTABLE ON)
endif()

# 新增：独立哈希抽样测试程序
add_executable(hash_test test/hash_test.cpp)
if(WIN32)
    target_link_libraries(hash_test PRIVATE ws2_32)
    # 隐藏独立验证程序的控制台
    set_target_properties(hash_test PROPERTIES WIN32_EXECUTABLE ON)
endif()