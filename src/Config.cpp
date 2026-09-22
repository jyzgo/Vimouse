#include "Config.h"
#include "State.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <map>

std::vector<RemoteHostInfo> g_remoteHosts;

static std::string ProfileDir() {
    char home[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, home) != S_OK) return "";
    return home;
}

std::string GetConfigDir() {
    std::string dir = ProfileDir() + "\\.vimouse";
    CreateDirectoryA(dir.c_str(), NULL);
    return dir;
}

std::string ConfigFilePath(const char* name) {
    return GetConfigDir() + "\\" + name;
}

// 极简 ini：忽略 [section]，key=value 一行一个
static std::map<std::string, std::string> ReadIni(const std::string& path) {
    std::map<std::string, std::string> kv;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        auto trim = [](std::string& s) {
            size_t a = s.find_first_not_of(" \t\r");
            size_t b = s.find_last_not_of(" \t\r");
            s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
        };
        trim(k); trim(v);
        kv[k] = v;
    }
    return kv;
}

static bool IniBool(const std::map<std::string, std::string>& kv, const char* key, bool def) {
    auto it = kv.find(key);
    if (it == kv.end()) return def;
    return it->second == "1" || it->second == "true" || it->second == "yes";
}

void LoadSettings() {
    auto kv = ReadIni(ConfigFilePath("settings.ini"));
    g_settings.keyOsd = IniBool(kv, "key_osd", false);
    g_settings.customCursor = IniBool(kv, "custom_cursor", true);
}

void SaveSettings() {
    std::ofstream f(ConfigFilePath("settings.ini"));
    f << "# Vimouse settings\n";
    f << "key_osd=" << (g_settings.keyOsd ? 1 : 0) << "\n";
    f << "custom_cursor=" << (g_settings.customCursor ? 1 : 0) << "\n";
}

void LoadRemoteHosts() {
    g_remoteHosts.clear();
    std::string path = ConfigFilePath("remote_hosts.txt");
    std::ifstream f(path);
    if (!f.is_open()) f.open(ConfigFilePath(".remote_hosts"));  // 旧版文件名
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t sep = line.find('|');
        RemoteHostInfo info;
        if (sep != std::string::npos) {
            info.host = line.substr(0, sep);
            info.exePath = line.substr(sep + 1);
        } else {
            info.host = line;
            info.exePath = "Vimouse.exe";
        }
        g_remoteHosts.push_back(info);
    }
}

void SaveRemoteHosts() {
    std::ofstream f(ConfigFilePath("remote_hosts.txt"));
    f << "# host|remote_vimouse_exe_path\n";
    for (const auto& h : g_remoteHosts) f << h.host << "|" << h.exePath << "\n";
}

static const char* kRunKey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";

bool IsAutoStartEnabled() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    char buf[MAX_PATH];
    DWORD size = sizeof(buf);
    bool exists = RegQueryValueExA(hKey, "Vimouse", NULL, NULL, (LPBYTE)buf, &size) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    return exists;
}

void SetAutoStart(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, kRunKey, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS) return;
    if (enable) {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        std::string quoted = std::string("\"") + path + "\"";
        RegSetValueExA(hKey, "Vimouse", 0, REG_SZ, (const BYTE*)quoted.c_str(), (DWORD)quoted.size() + 1);
    } else {
        RegDeleteValueA(hKey, "Vimouse");
    }
    RegCloseKey(hKey);
}
