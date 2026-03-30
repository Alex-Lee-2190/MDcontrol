#include "Translation.h"
#include "Common.h"
#include "SystemUtils.h"
#include <map>

static std::map<QString, QString> dict_en_US = {
    {"提示", "Notice"},
    {"多设备连接时，同时只能进行一个文件传输任务。", "Only one file transfer task is allowed at a time when multiple devices are connected."},
    {"文件读取失败", "File Read Error"},
    {"无法读取文件:\n%1\n可能文件被删除或磁盘已断开。", "Unable to read file:\n%1\nIt might have been deleted or the disk disconnected."},
    {"重试", "Retry"},
    {"跳过", "Skip"},
    {"取消传输", "Cancel Transfer"},
    {"文件重复", "File Conflict"},
    {"目标路径已有 %1 个相同名称的文件。", "Destination already has %1 file(s) with the same name."},
    {"替换目标中的文件", "Replace files in the destination"},
    {"跳过这些文件", "Skip these files"},
    {"让我决定每个文件", "Let me decide for each file"},
    {"文件传输 - ", "File Transfer - "},
    {"已完成 0%", "0% Complete"},
    {"暂停/继续", "Pause/Resume"},
    {"目标设备: %1", "Target Device: %1"},
    {"保存至: %1", "Save to: %1"},
    {"名称: ", "Name: "},
    {"名称: %1", "Name: %1"},
    {"剩余时间: 计算中...", "Time remaining: Calculating..."},
    {"剩余项目: %1 (%2)", "Items remaining: %1 (%2)"},
    {"已完成 %1%", "%1% Complete"},
    {"速度: %1", "Speed: %1"},
    {"剩余时间: 大约 %1 秒", "Time remaining: About %1 seconds"},
    {"剩余时间: 大约 %1 分 %2 秒", "Time remaining: About %1 min %2 sec"},
    {"正在统计元数据...", "Counting metadata..."},
    {"正在扫描文件...", "Scanning files..."},
    {"剩余时间: 统计中...", "Time remaining: Counting..."},
    {"已扫描: %1 (%2)", "Scanned: %1 (%2)"},
    {"配对验证", "Pairing Verification"},
    {"请确认并在两端输入相同的6位数字PIN码\n与设备 [%1]配对:", "Please confirm and enter the same 6-digit PIN code on both sides\nto pair with device [%1]:"},
    {"请输入6位数字PIN码", "Please enter a 6-digit PIN code"},
    {"设备连接", "Device Connection"},
    {"作为主控端连接", "Connect as Master"},
    {"网络", "Network"},
    {"蓝牙", "Bluetooth"},
    {"IP:", "IP:"},
    {"端口:", "Port:"},
    {"连接", "Connect"},
    {"监听其它主控端连接", "Listen for Master connections"},
    {"就绪", "Ready"},
    {"打开主界面", "Open Main Window"},
    {"延迟: %1 ms", "Latency: %1 ms"},
    {"已连接设备", "Connected Devices"},
    {"无设备", "No devices"},
    {"主控端", "Master"},
    {"断开连接", "Disconnect"},
    {"退出", "Exit"},
    {"扫描", "Scan"},
    {"当前TCP网络已断开，是否继续使用蓝牙传输文件？\n（蓝牙传输速度较慢）", "TCP network disconnected. Continue using Bluetooth for file transfer?\n(Bluetooth transfer is slower)"},
    {"不再提示", "Do not show again"},
    {"错误", "Error"},
    {"TCP控制通道连接失败", "TCP Control Channel connection failed"},
    {"连接失败，保持现有连接", "Connection failed, keeping current connections"},
    {"TCP文件通道连接失败", "TCP File Channel connection failed"},
    {"配对失败", "Pairing Failed"},
    {"验证未通过或已取消。", "Verification failed or cancelled."},
    {"已作为被控端连接", "Connected as Slave"},
    {"正在监听连接", "Listening for connections"},
    {"已连接: %1", "Connected: %1"},
    {"设置", "Settings"},
    {"常规设置", "General"},
    {"语言", "Language"},
    {"自动", "Auto"},
    {"中文", "Chinese"},
    {"English", "English"},
    {"快捷键", "Hotkeys"},
    {"动作", "Action"},
    {"修饰键", "Modifier Key"},
    {"主键", "Main Key"},
    {"无", "None"},
    {"点击后按键设定", "Click to set key"},
    {"点击此处然后按下想要绑定的按键(字母/数字等)，按Esc清除", "Click here and press desired key, press Esc to clear"},
    {"显示/隐藏主界面", "Show/Hide Main Window"},
    {"锁定/解锁设备", "Lock/Unlock Device"},
    {"本机断开连接", "Disconnect Local Machine"},
    {"终止程序", "Terminate Program"},
    {"传输与连接", "Transfer and Connection"},
    {"备用保存路径:", "Fallback Save Path:"},
    {"浏览", "Browse"},
    {"选择备用路径", "Select Fallback Path"},
    {"记住上次连接的被控端位置", "Remember previous Slave positions"},
    {"调试与日志", "Debug and Logs"},
    {"保存日志", "Save Logs"},
    {"日志级别", "Log Level"},
    {"正在连接", "Connecting"},
    {"正在扫描", "Scanning"},
    {"清除历史与配稳记录", "Clear history and pairing records"},
    {"未知时间", "Unknown time"},
    {"%1 字节", "%1 Bytes"},
    {"文件来自于 源", "File from Source"},
    {"文件已位于 目标", "File already in Destination"},
    {"<b>你要保留哪些文件？</b><br>请勾选你要保留的文件。", "<b>Which files do you want to keep?</b><br>Check the files you want to keep."},
    {"全部保留源文件", "Keep all source files"},
    {"全部保留目标文件", "Keep all destination files"},
    {"共 %1 个文件冲突", "%1 file conflicts in total"},
    {"远程 (主机)", "Remote (Master)"},
    {"本地 (主机)", "Local (Master)"},
    {"全局", "Global"},
    {"锁定设备", "Lock Device"},
    {"解锁设备", "Unlock Device"},
    {"继续", "Resume"},
    {"暂停", "Pause"},
    {"重连网络", "Reconnect Network"},
    {"重连", "Reconnect"},
    {"属性", "Properties"},
    {"无属性数据", "No property data"},
    {"设备属性", "Device Properties"},
    {"设备连接", "Device Connection"},
    {"暂停/继续设备", "Pause/Resume Device"},
    {"重连网络控制通道", "Reconnect Network Control Channel"},
    {"重连文件传输通道", "Reconnect File Transfer Channel"},
    {"居中视图", "Center View"},
    {"放大视图", "Zoom In"},
    {"缩小视图", "Zoom Out"},
    {"蓝牙未打开或不可用", "Bluetooth is disabled or unavailable"},
    {"网络未连接或不可用", "Network is disconnected or unavailable"}
};

// static std::map<QString, QString> dict_ja_JP = { ... };

QString T(const QString& sourceText) {
    static int currentLang = -1;
    if (currentLang == -1 || currentLang != g_Language) {
        if (g_Language == 0) {
            currentLang = (SystemUtils::GetSystemLanguage() == "zh") ? 1 : 2;
        } else {
            currentLang = g_Language;
        }
    }
    if (currentLang == 1) {
        return sourceText;
    }
    if (currentLang == 2) {
        auto it = dict_en_US.find(sourceText);
        if (it != dict_en_US.end()) {
            return it->second;
        }
    }
    return sourceText;
}