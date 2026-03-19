#include "CommandHandler.h"
#include "WindowQuery.h"
#include "UIAutomation.h"
#include "ScreenOCR.h"
#include <sstream>
#include <fstream>
#include <thread>
#include <chrono>

// 辅助：分割字符串
static std::vector<std::string> SplitCommand(const std::string& cmd) {
    std::vector<std::string> parts;
    std::istringstream iss(cmd);
    std::string token;
    while (iss >> token) {
        parts.push_back(token);
    }
    return parts;
}

// 辅助：安全转 int
static bool ParseInt(const std::string& s, int& out) {
    try {
        out = std::stoi(s);
        return true;
    } catch (...) {
        return false;
    }
}

static void DoMouseClick(int x, int y, bool move, DWORD downFlag, DWORD upFlag) {
    if (move) {
        SetCursorPos(x, y);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    mouse_event(downFlag, 0, 0, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    mouse_event(upFlag, 0, 0, 0, 0);
}

std::string HandleCommand(const std::string& command) {
    auto parts = SplitCommand(command);
    if (parts.empty()) {
        return "ERR empty command";
    }

    const std::string& cmd = parts[0];

    // move x y
    if (cmd == "move") {
        if (parts.size() < 3) return "ERR move requires x y";
        int x, y;
        if (!ParseInt(parts[1], x) || !ParseInt(parts[2], y))
            return "ERR invalid coordinates";
        SetCursorPos(x, y);
        return "OK";
    }

    // click [x y] [button]
    if (cmd == "click") {
        int x, y;
        bool hasPos = false;
        if (parts.size() >= 3 && ParseInt(parts[1], x) && ParseInt(parts[2], y)) {
            hasPos = true;
        } else {
            POINT p; GetCursorPos(&p);
            x = p.x; y = p.y;
        }
        DoMouseClick(x, y, hasPos, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
        return "OK";
    }

    // rclick [x y]
    if (cmd == "rclick") {
        int x, y;
        bool hasPos = false;
        if (parts.size() >= 3 && ParseInt(parts[1], x) && ParseInt(parts[2], y)) {
            hasPos = true;
        } else {
            POINT p; GetCursorPos(&p);
            x = p.x; y = p.y;
        }
        DoMouseClick(x, y, hasPos, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
        return "OK";
    }

    // dclick [x y]
    if (cmd == "dclick") {
        int x, y;
        bool hasPos = false;
        if (parts.size() >= 3 && ParseInt(parts[1], x) && ParseInt(parts[2], y)) {
            hasPos = true;
        } else {
            POINT p; GetCursorPos(&p);
            x = p.x; y = p.y;
        }
        DoMouseClick(x, y, hasPos, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        DoMouseClick(x, y, false, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
        return "OK";
    }

    // drag x1 y1 x2 y2 [duration_ms]
    if (cmd == "drag") {
        if (parts.size() < 5) return "ERR drag requires x1 y1 x2 y2";
        int x1, y1, x2, y2, duration = 300;
        if (!ParseInt(parts[1], x1) || !ParseInt(parts[2], y1) ||
            !ParseInt(parts[3], x2) || !ParseInt(parts[4], y2))
            return "ERR invalid coordinates";
        if (parts.size() >= 6) ParseInt(parts[5], duration);

        SetCursorPos(x1, y1);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);

        // 平滑拖拽
        int steps = duration / 10;
        if (steps < 1) steps = 1;
        for (int i = 1; i <= steps; i++) {
            int cx = x1 + (x2 - x1) * i / steps;
            int cy = y1 + (y2 - y1) * i / steps;
            SetCursorPos(cx, cy);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        return "OK";
    }

    // scroll up|down|left|right [amount]
    if (cmd == "scroll") {
        if (parts.size() < 2) return "ERR scroll requires direction";
        int amount = 3;
        if (parts.size() >= 3) ParseInt(parts[2], amount);
        int delta = amount * 120; // WHEEL_DELTA = 120

        if (parts[1] == "up") {
            mouse_event(MOUSEEVENTF_WHEEL, 0, 0, delta, 0);
        } else if (parts[1] == "down") {
            mouse_event(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)(-delta), 0);
        } else if (parts[1] == "left") {
            mouse_event(MOUSEEVENTF_HWHEEL, 0, 0, (DWORD)(-delta), 0);
        } else if (parts[1] == "right") {
            mouse_event(MOUSEEVENTF_HWHEEL, 0, 0, delta, 0);
        } else {
            return "ERR unknown direction: " + parts[1];
        }
        return "OK";
    }

    // pos - 返回当前鼠标位置
    if (cmd == "pos") {
        POINT p;
        GetCursorPos(&p);
        return "OK " + std::to_string(p.x) + " " + std::to_string(p.y);
    }

    // tags - 列出所有标签
    if (cmd == "tags") {
        std::string result = "OK [";
        for (size_t i = 0; i < g_tags.size(); i++) {
            if (i > 0) result += ",";
            result += "{\"letter\":\"";
            result += g_tags[i].letter;
            result += "\",\"x\":" + std::to_string(g_tags[i].pos.x);
            result += ",\"y\":" + std::to_string(g_tags[i].pos.y) + "}";
        }
        result += "]";
        return result;
    }

    // tag <letter> [click]
    if (cmd == "tag") {
        if (parts.size() < 2) return "ERR tag requires letter";
        char letter = (char)toupper(parts[1][0]);
        for (const auto& t : g_tags) {
            if (t.letter == letter) {
                SetCursorPos(t.pos.x, t.pos.y);
                if (parts.size() >= 3 && parts[2] == "click") {
                    std::this_thread::sleep_for(std::chrono::milliseconds(30));
                    DoMouseClick(t.pos.x, t.pos.y, false,
                                 MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
                }
                return "OK " + std::to_string(t.pos.x) + " " + std::to_string(t.pos.y);
            }
        }
        return "ERR tag not found: " + std::string(1, letter);
    }

    // sleep <ms>
    if (cmd == "sleep") {
        if (parts.size() < 2) return "ERR sleep requires ms";
        int ms;
        if (!ParseInt(parts[1], ms)) return "ERR invalid duration";
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return "OK";
    }

    // 窗口查询命令
    if (cmd == "get_active_window") {
        return "OK " + GetActiveWindowInfo();
    }

    if (cmd == "list_windows") {
        return "OK " + ListWindows();
    }

    // find_window <title_contains>
    if (cmd == "find_window") {
        if (parts.size() < 2) return "ERR find_window requires title";
        // 拼接后续所有部分作为搜索关键字
        std::string title;
        for (size_t i = 1; i < parts.size(); i++) {
            if (i > 1) title += " ";
            title += parts[i];
        }
        return FindWindow(title);
    }

    // wait_window <title_contains> [timeout_ms]
    if (cmd == "wait_window") {
        if (parts.size() < 2) return "ERR wait_window requires title";
        int timeout = 30000;
        // 最后一个参数可能是数字(timeout)
        std::string title;
        size_t titleEnd = parts.size();
        if (parts.size() >= 3) {
            int t;
            if (ParseInt(parts.back(), t)) {
                timeout = t;
                titleEnd = parts.size() - 1;
            }
        }
        for (size_t i = 1; i < titleEnd; i++) {
            if (i > 1) title += " ";
            title += parts[i];
        }
        return WaitForWindow(title, timeout);
    }

    // UI Automation 命令
    // find_element <name> [type]
    if (cmd == "find_element") {
        if (parts.size() < 2) return "ERR find_element requires name";
        std::string name = parts[1];
        std::string type = parts.size() >= 3 ? parts[2] : "";
        return FindUIElement(name, type);
    }

    // list_elements [hwnd_hex] [depth]
    if (cmd == "list_elements") {
        HWND hwnd = GetForegroundWindow();
        int depth = 2;
        if (parts.size() >= 2) {
            unsigned long long h = std::stoull(parts[1], nullptr, 16);
            hwnd = (HWND)h;
        }
        if (parts.size() >= 3) ParseInt(parts[2], depth);
        return ListUIElements(hwnd, depth);
    }

    // click_element <name> [type]
    if (cmd == "click_element") {
        if (parts.size() < 2) return "ERR click_element requires name";
        std::string name = parts[1];
        std::string type = parts.size() >= 3 ? parts[2] : "";
        return ClickUIElement(name, type);
    }

    // OCR 命令
    // scan_region x1 y1 x2 y2
    if (cmd == "scan_region") {
        if (parts.size() < 5) return "ERR scan_region requires x1 y1 x2 y2";
        int x1, y1, x2, y2;
        if (!ParseInt(parts[1], x1) || !ParseInt(parts[2], y1) ||
            !ParseInt(parts[3], x2) || !ParseInt(parts[4], y2))
            return "ERR invalid coordinates";
        return ScanRegion(x1, y1, x2, y2);
    }

    // read_at [x y] [width height]
    if (cmd == "read_at") {
        if (parts.size() >= 3) {
            int x, y, w = 300, h = 60;
            if (!ParseInt(parts[1], x) || !ParseInt(parts[2], y))
                return "ERR invalid coordinates";
            if (parts.size() >= 5) {
                ParseInt(parts[3], w);
                ParseInt(parts[4], h);
            }
            return ReadAt(x, y, w, h);
        }
        return ReadAtCursor();
    }

    // screenshot [x1 y1 x2 y2] [quality]
    // 截取屏幕区域保存为 JPEG，返回文件路径
    if (cmd == "screenshot") {
        int sx1 = 0, sy1 = 0, sx2 = GetSystemMetrics(SM_CXVIRTUALSCREEN), sy2 = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        int quality = 70;
        if (parts.size() >= 5) {
            ParseInt(parts[1], sx1); ParseInt(parts[2], sy1);
            ParseInt(parts[3], sx2); ParseInt(parts[4], sy2);
        }
        if (parts.size() >= 6) {
            ParseInt(parts[5], quality);
        }
        return CaptureScreenJPEG(sx1, sy1, sx2, sy2, quality);
    }

    return "ERR unknown command: " + cmd;
}
