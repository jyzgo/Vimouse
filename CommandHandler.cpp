#include "CommandHandler.h"
#include <sstream>
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

// 辅助：解析按键名 -> VK code
static WORD NameToVK(const std::string& name) {
    if (name == "enter" || name == "return") return VK_RETURN;
    if (name == "tab") return VK_TAB;
    if (name == "escape" || name == "esc") return VK_ESCAPE;
    if (name == "backspace" || name == "bs") return VK_BACK;
    if (name == "delete" || name == "del") return VK_DELETE;
    if (name == "space") return VK_SPACE;
    if (name == "up") return VK_UP;
    if (name == "down") return VK_DOWN;
    if (name == "left") return VK_LEFT;
    if (name == "right") return VK_RIGHT;
    if (name == "home") return VK_HOME;
    if (name == "end") return VK_END;
    if (name == "pageup" || name == "pgup") return VK_PRIOR;
    if (name == "pagedown" || name == "pgdn") return VK_NEXT;
    if (name == "insert" || name == "ins") return VK_INSERT;
    if (name == "f1") return VK_F1;
    if (name == "f2") return VK_F2;
    if (name == "f3") return VK_F3;
    if (name == "f4") return VK_F4;
    if (name == "f5") return VK_F5;
    if (name == "f6") return VK_F6;
    if (name == "f7") return VK_F7;
    if (name == "f8") return VK_F8;
    if (name == "f9") return VK_F9;
    if (name == "f10") return VK_F10;
    if (name == "f11") return VK_F11;
    if (name == "f12") return VK_F12;
    if (name == "ctrl") return VK_CONTROL;
    if (name == "alt") return VK_MENU;
    if (name == "shift") return VK_SHIFT;
    if (name == "win") return VK_LWIN;
    // 单字符
    if (name.size() == 1) {
        char c = (char)toupper(name[0]);
        if (c >= 'A' && c <= 'Z') return (WORD)c;
        if (c >= '0' && c <= '9') return (WORD)c;
    }
    return 0;
}

// 辅助：模拟按键（支持组合键如 ctrl+c）
static void SimulateKeyCombo(const std::string& combo) {
    // 按 '+' 分割
    std::vector<std::string> keys;
    std::istringstream iss(combo);
    std::string part;
    while (std::getline(iss, part, '+')) {
        // 转小写
        std::string lower;
        for (char c : part) lower += (char)tolower(c);
        keys.push_back(lower);
    }

    // 按下修饰键
    std::vector<WORD> modifiers;
    for (size_t i = 0; i + 1 < keys.size(); i++) {
        WORD vk = NameToVK(keys[i]);
        if (vk) {
            keybd_event((BYTE)vk, 0, 0, 0);
            modifiers.push_back(vk);
        }
    }

    // 按下并释放主键
    if (!keys.empty()) {
        WORD vk = NameToVK(keys.back());
        if (vk) {
            keybd_event((BYTE)vk, 0, 0, 0);
            keybd_event((BYTE)vk, 0, KEYEVENTF_KEYUP, 0);
        }
    }

    // 释放修饰键（逆序）
    for (int i = (int)modifiers.size() - 1; i >= 0; i--) {
        keybd_event((BYTE)modifiers[i], 0, KEYEVENTF_KEYUP, 0);
    }
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

    // click [x y]
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
        int delta = amount * 120;

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

    // pos
    if (cmd == "pos") {
        POINT p;
        GetCursorPos(&p);
        return "OK " + std::to_string(p.x) + " " + std::to_string(p.y);
    }

    // keypress <combo> - 模拟组合键，如 keypress ctrl+c, keypress alt+f4, keypress enter
    if (cmd == "keypress") {
        if (parts.size() < 2) return "ERR keypress requires key combo (e.g. ctrl+c)";
        SimulateKeyCombo(parts[1]);
        return "OK";
    }

    // type <text> - 模拟输入文本（支持 UTF-8）
    if (cmd == "type") {
        if (parts.size() < 2) return "ERR type requires text";
        // 拼接所有参数（保留空格）
        std::string text;
        for (size_t i = 1; i < parts.size(); i++) {
            if (i > 1) text += " ";
            text += parts[i];
        }
        // 转为宽字符并逐个发送
        int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
        if (wlen <= 0) return "ERR invalid text";
        std::vector<wchar_t> wtext(wlen);
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wtext.data(), wlen);
        for (int i = 0; i < wlen - 1; i++) {
            INPUT input = {};
            input.type = INPUT_KEYBOARD;
            input.ki.wScan = wtext[i];
            input.ki.dwFlags = KEYEVENTF_UNICODE;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        return "OK";
    }

    // tags
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

    // mclick [x y] - 中键点击
    if (cmd == "mclick") {
        int x, y;
        bool hasPos = false;
        if (parts.size() >= 3 && ParseInt(parts[1], x) && ParseInt(parts[2], y)) {
            hasPos = true;
        } else {
            POINT p; GetCursorPos(&p);
            x = p.x; y = p.y;
        }
        if (hasPos) {
            SetCursorPos(x, y);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        mouse_event(MOUSEEVENTF_MIDDLEDOWN, 0, 0, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        mouse_event(MOUSEEVENTF_MIDDLEUP, 0, 0, 0, 0);
        return "OK";
    }

    // status - 返回当前状态
    if (cmd == "status") {
        POINT p;
        GetCursorPos(&p);
        std::string result = "OK {\"active\":" + std::string(g_isActive ? "true" : "false");
        result += ",\"x\":" + std::to_string(p.x);
        result += ",\"y\":" + std::to_string(p.y) + "}";
        return result;
    }

    // activate - 激活 Vimouse 键盘控制
    if (cmd == "activate") {
        g_isActive = true;
        return "OK";
    }

    // deactivate - 停用 Vimouse 键盘控制
    if (cmd == "deactivate") {
        g_isActive = false;
        return "OK";
    }

    // screen - 返回屏幕信息
    if (cmd == "screen") {
        POINT p;
        GetCursorPos(&p);
        HMONITOR hMon = MonitorFromPoint(p, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMon, &mi);
        int w = mi.rcMonitor.right - mi.rcMonitor.left;
        int h = mi.rcMonitor.bottom - mi.rcMonitor.top;
        std::string result = "OK {\"width\":" + std::to_string(w);
        result += ",\"height\":" + std::to_string(h);
        result += ",\"left\":" + std::to_string(mi.rcMonitor.left);
        result += ",\"top\":" + std::to_string(mi.rcMonitor.top) + "}";
        return result;
    }

    // help - 列出所有可用命令
    if (cmd == "help") {
        return "OK commands: move click rclick dclick mclick drag scroll pos keypress type tags tag sleep status activate deactivate screen help";
    }

    return "ERR unknown command: " + cmd;
}
