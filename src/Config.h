// Config.h - 配置目录与持久化（%USERPROFILE%\.vimouse\）
#pragma once
#include <string>
#include <vector>
#include "State.h"

struct RemoteHostInfo {
    std::string host;
    std::string exePath;
};

// 配置目录（不存在则创建），如 C:\Users\me\.vimouse
std::string GetConfigDir();
// 配置目录下的文件路径
std::string ConfigFilePath(const char* name);

// 通用设置 settings.ini
void LoadSettings();
void SaveSettings();

// 速度参数表：设置对话框「速度」页和 settings.ini 共用（默认值取 Settings{} 的成员初值）
struct SpeedParam {
    const char*    key;      // settings.ini 键名
    const wchar_t* zh;
    const wchar_t* en;
    int Settings::* field;
    int lo, hi;
    bool scroll;             // false = 鼠标移动组，true = 滚轮组
};
extern const SpeedParam kSpeedParams[];
extern const int kSpeedParamCount;
// 把 g_settings 里的移动速度推到移动线程用的全局变量（加载/保存设置后调用）
void ApplySpeedSettings();

// 远程主机列表 remote_hosts.txt
extern std::vector<RemoteHostInfo> g_remoteHosts;
void LoadRemoteHosts();
void SaveRemoteHosts();

// 开机自启（HKCU\...\Run）
bool IsAutoStartEnabled();
void SetAutoStart(bool enable);
