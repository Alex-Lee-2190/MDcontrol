#include <windows.h>
#include <setupapi.h>
#include <iostream>
#include <string>
#include <vector>

// 编译指令参考: g++ test/bt_disconnect.cpp -o bt_disconnect.exe -lsetupapi
#pragma comment(lib, "setupapi.lib")

// 改变指定设备节点的状态
bool ChangeDeviceState(HDEVINFO hDevInfo, SP_DEVINFO_DATA& devInfoData, DWORD state) {
    SP_PROPCHANGE_PARAMS pcp;
    pcp.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    pcp.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    pcp.StateChange = state; // DICS_DISABLE 或 DICS_ENABLE
    pcp.Scope = DICS_FLAG_GLOBAL;
    pcp.HwProfile = 0;

    if (!SetupDiSetClassInstallParams(hDevInfo, &devInfoData, &pcp.ClassInstallHeader, sizeof(pcp))) {
        return false;
    }
    if (!SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, hDevInfo, &devInfoData)) {
        return false;
    }
    return true;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    std::cout << "========== 蓝牙断连模拟测试工具 (RFCOMM协议层阻断) ==========" << std::endl;
    std::cout << "注意：本工具需要【管理员权限】运行！" << std::endl;
    std::cout << "即将切断底层的 RFCOMM TDI 协议传输，这会摧毁所有活动 Socket 但保持蓝牙开启..." << std::endl;
    
    // 按照您的要求，运行后 10 秒内断开连接
    for (int i = 10; i > 0; --i) {
        std::cout << "倒计时: " << i << " 秒..." << std::endl;
        Sleep(1000);
    }

    // 搜索系统中所有的设备
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) {
        std::cout << "[错误] 获取设备列表失败！" << std::endl;
        system("pause");
        return 1;
    }

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    
    bool found = false;
    std::vector<SP_DEVINFO_DATA> targetNodes;

    // 遍历抓取 RFCOMM 协议层驱动节点
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
        char devId[512];
        if (SetupDiGetDeviceInstanceIdA(hDevInfo, &devInfoData, devId, sizeof(devId), NULL)) {
            std::string idStr(devId);
            
            // 锁定 "BTH\MS_RFCOMM" (Bluetooth Device (RFCOMM Protocol TDI))
            // 锁定 "BTH\MS_BTHPAN" (Bluetooth Device (Personal Area Network)) 补充防漏
            if (idStr.find("BTH\\MS_RFCOMM") != std::string::npos || 
                idStr.find("BTH\\MS_BTHPAN") != std::string::npos) {
                
                std::cout << "[发现协议驱动节点] " << idStr << std::endl;
                targetNodes.push_back(devInfoData);
            }
        }
    }

    if (targetNodes.empty()) {
        std::cout << "[结果] 未发现 RFCOMM 协议节点。请确认是否拥有管理员权限。" << std::endl;
    } else {
        std::cout << "\n>>> 正在挂起 RFCOMM 协议层 (Disable Node)... <<<" << std::endl;
        for (auto& node : targetNodes) {
            if (ChangeDeviceState(hDevInfo, node, DICS_DISABLE)) {
                std::cout << "[OK] 挂起成功，底层 Socket 流已阻断！" << std::endl;
                found = true;
            } else {
                std::cout << "  [FAIL] 挂起失败！" << std::endl;
            }
        }

        if (found) {
            // 维持断开状态 3 秒，这足够触发 MDControl 主程序的 recv() 返回 SOCKET_ERROR
            std::cout << "\n>>> 保持断路状态 3 秒，请观察 MDControl 界面是否显示断开图标... <<<" << std::endl;
            Sleep(3000); 

            std::cout << "\n>>> 正在恢复 RFCOMM 协议层 (Enable Node)... <<<" << std::endl;
            for (auto& node : targetNodes) {
                ChangeDeviceState(hDevInfo, node, DICS_ENABLE);
            }
            std::cout << ">>> 恢复完成，现在可以右键点击界面图标进行【重连】测试了 <<<" << std::endl;
        }
    }
    
    SetupDiDestroyDeviceInfoList(hDevInfo);
    
    std::cout << std::endl;
    system("pause");
    return 0;
}