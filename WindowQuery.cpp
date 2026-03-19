#include "WindowQuery.h"
#include <thread>
#include <chrono>

// 辅助：宽字符转 UTF-8
static std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), NULL, 0, NULL, NULL);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), &result[0], size, NULL, NULL);
    return result;
}

// 辅助：JSON 转义字符串
static std::string JsonEscape(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 10);
    for (char c : s) {
        switch (c) {
        case '"':  result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:   result += c; break;
        }
    }
    return result;
}

// 辅助：获取窗口标题 (UTF-8)
static std::string GetWindowTitleUtf8(HWND hwnd) {
    wchar_t title[512] = {};
    GetWindowTextW(hwnd, title, 512);
    return WideToUtf8(title);
}

// 辅助：获取窗口类名 (UTF-8)
static std::string GetWindowClassUtf8(HWND hwnd) {
    wchar_t cls[256] = {};
    GetClassNameW(hwnd, cls, 256);
    return WideToUtf8(cls);
}

// 辅助：构建窗口信息 JSON
static std::string WindowInfoJson(HWND hwnd) {
    std::string title = GetWindowTitleUtf8(hwnd);
    std::string cls = GetWindowClassUtf8(hwnd);
    RECT rect;
    GetWindowRect(hwnd, &rect);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    char hwndStr[20];
    snprintf(hwndStr, sizeof(hwndStr), "0x%llX", (unsigned long long)hwnd);

    std::string json = "{";
    json += "\"title\":\"" + JsonEscape(title) + "\"";
    json += ",\"class\":\"" + JsonEscape(cls) + "\"";
    json += ",\"hwnd\":\"" + std::string(hwndStr) + "\"";
    json += ",\"pid\":" + std::to_string(pid);
    json += ",\"rect\":[" + std::to_string(rect.left) + ","
        + std::to_string(rect.top) + ","
        + std::to_string(rect.right) + ","
        + std::to_string(rect.bottom) + "]";
    json += ",\"visible\":" + std::string(IsWindowVisible(hwnd) ? "true" : "false");
    json += "}";
    return json;
}

std::string GetActiveWindowInfo() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return "{}";
    return WindowInfoJson(hwnd);
}

// 回调数据
struct EnumWindowsData {
    std::string result;
    int count;
    std::string filter; // 如果非空，按标题过滤
};

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* data = (EnumWindowsData*)lParam;

    if (!IsWindowVisible(hwnd)) return TRUE;

    std::string title = GetWindowTitleUtf8(hwnd);
    if (title.empty()) return TRUE;

    // 如果有过滤条件
    if (!data->filter.empty()) {
        // 大小写不敏感搜索
        std::string titleLower = title;
        std::string filterLower = data->filter;
        std::transform(titleLower.begin(), titleLower.end(), titleLower.begin(), ::tolower);
        std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
        if (titleLower.find(filterLower) == std::string::npos) return TRUE;
    }

    if (data->count > 0) data->result += ",";
    data->result += WindowInfoJson(hwnd);
    data->count++;

    return TRUE;
}

std::string ListWindows() {
    EnumWindowsData data;
    data.count = 0;
    data.result = "[";
    EnumWindows(EnumWindowsProc, (LPARAM)&data);
    data.result += "]";
    return data.result;
}

std::string FindWindow(const std::string& titleContains) {
    EnumWindowsData data;
    data.count = 0;
    data.filter = titleContains;
    data.result = "[";
    EnumWindows(EnumWindowsProc, (LPARAM)&data);
    data.result += "]";

    if (data.count == 0) {
        return "ERR window not found: " + titleContains;
    }
    return "OK " + data.result;
}

std::string WaitForWindow(const std::string& titleContains, int timeoutMs) {
    auto start = std::chrono::steady_clock::now();

    while (true) {
        EnumWindowsData data;
        data.count = 0;
        data.filter = titleContains;
        data.result = "[";
        EnumWindows(EnumWindowsProc, (LPARAM)&data);
        data.result += "]";

        if (data.count > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            return "OK {\"found\":true,\"waited_ms\":" + std::to_string(elapsed)
                + ",\"windows\":" + data.result + "}";
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeoutMs) {
            return "ERR timeout waiting for window: " + titleContains;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
