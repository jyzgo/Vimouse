// Config.h - 配置目录与持久化（%USERPROFILE%\.vimouse\）
#pragma once
#include <string>
#include <vector>

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

// 远程主机列表 remote_hosts.txt
extern std::vector<RemoteHostInfo> g_remoteHosts;
void LoadRemoteHosts();
void SaveRemoteHosts();

// 开机自启（HKCU\...\Run）
bool IsAutoStartEnabled();
void SetAutoStart(bool enable);
