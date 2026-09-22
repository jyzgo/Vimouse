#include "HelpOverlay.h"
#include "State.h"
#include "Config.h"
#include "Keymap.h"
#include "Overlay.h"
#include "Util.h"
#include <fstream>
#include <vector>
#include <string>

static HFONT g_font = NULL;
static const int kW = 230, kLineH = 15;

static std::string PosFile() { return ConfigFilePath("help_pos.txt"); }

static void SavePos() {
    if (!g_helpWindow) return;
    RECT r; GetWindowRect(g_helpWindow, &r);
    std::ofstream f(PosFile());
    f << r.left << " " << r.top << " " << (g_helpVisible ? 1 : 0) << "\n";
}

static void LoadPos(int& x, int& y, bool& visible) {
    x = y = -1; visible = false;
    std::ifstream f(PosFile());
    int v = 0;
    if (f >> x >> y >> v) visible = v != 0;
}

// 按当前 keymap 生成行
static std::vector<std::wstring> Lines(bool zh) {
    auto K = [](Action a) { return ChordToString(g_keymap[(int)a]); };
    auto R = [&](Action a1, Action a2) { return VkName(g_keymap[(int)a1].vk) + L"/" + VkName(g_keymap[(int)a2].vk); };
    std::wstring mv = VkName(g_keymap[(int)Action::MoveLeft].vk) + L"/" + VkName(g_keymap[(int)Action::MoveDown].vk) + L"/" +
                      VkName(g_keymap[(int)Action::MoveUp].vk) + L"/" + VkName(g_keymap[(int)Action::MoveRight].vk);
    std::wstring dg = VkName(g_keymap[(int)Action::MoveUpLeft].vk) + L"/" + VkName(g_keymap[(int)Action::MoveUpRight].vk) + L"/" +
                      VkName(g_keymap[(int)Action::MoveDownLeft].vk) + L"/" + VkName(g_keymap[(int)Action::MoveDownRight].vk);
    std::vector<std::wstring> L;
    L.push_back(zh ? L" Vimouse 快速参考" : L" Vimouse Quick Ref");
    L.push_back(L" ─────────────────────");
    L.push_back(L" " + K(Action::Toggle) + (zh ? L"  开关激活" : L"  Toggle ON/OFF"));
    L.push_back(L" " + mv + (zh ? L"  移动(长按加速)" : L"  Move (hold=accel)"));
    L.push_back(zh ? L" Shift+移动  精确1像素" : L" Shift+move  Precise 1px");
    L.push_back(L" " + dg + (zh ? L"  对角移动" : L"  Diagonal"));
    L.push_back(L" " + K(Action::ClickLeft) + (zh ? L"  左键(按住)" : L"  Left click (hold)"));
    L.push_back(L" " + K(Action::ClickRight) + (zh ? L"  右键" : L"  Right click"));
    L.push_back(L" " + K(Action::ClickMiddle) + (zh ? L"  中键" : L"  Middle click"));
    L.push_back(L" " + K(Action::DragToggle) + (zh ? L"  拖拽开关" : L"  Drag toggle"));
    L.push_back(L" " + K(Action::Hint) + (zh ? L"  Hint跳转(2字母)" : L"  Hint jump (2-letter)"));
    L.push_back(L" " + K(Action::Grid) + (zh ? L"  Grid二分定位" : L"  Grid bisect"));
    L.push_back(L" " + K(Action::WheelMode) + (zh ? L"  滚轮模式" : L"  Scroll mode"));
    L.push_back(L" " + K(Action::TagJump) + (zh ? L"  标签跳转" : L"  Tag jump"));
    L.push_back(L" " + K(Action::TagPut) + (zh ? L"  放置标签" : L"  Place tag"));
    L.push_back(L" " + K(Action::ScreenCenter) + (zh ? L"  屏幕中心/切屏" : L"  Screen center/switch"));
    L.push_back(L" " + R(Action::HistPrev, Action::HistNext) + (zh ? L"  历史位置" : L"  Prev/Next position"));
    L.push_back(zh ? L" Enter  点击+退出" : L" Enter  Click + exit");
    L.push_back(zh ? L" Esc    退出模式" : L" Esc    Exit mode");
    return L;
}

static LRESULT CALLBACK HelpWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        DoubleBuffer db(hwnd);
        HBRUSH bg = CreateSolidBrush(RGB(20, 20, 30));
        FillRect(db.mem, &db.rc, bg);
        DeleteObject(bg);
        if (!g_font) g_font = MakeFont(13, FW_NORMAL, L"Consolas", FIXED_PITCH | FF_MODERN);
        HGDIOBJ old = SelectObject(db.mem, g_font);
        auto lines = Lines(IsSystemChinese());
        for (size_t i = 0; i < lines.size(); i++) {
            RECT tr = { 4, 4 + (int)i * kLineH, db.rc.right - 4, 4 + (int)(i + 1) * kLineH };
            SetTextColor(db.mem, i == 0 ? RGB(80, 255, 160) : i == 1 ? RGB(60, 60, 80) : RGB(180, 180, 200));
            DrawTextW(db.mem, lines[i].c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(db.mem, old);
        return 0;
    }
    case WM_LBUTTONDOWN:
        SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    case WM_EXITSIZEMOVE:
        SavePos();
        return 0;
    case WM_CLOSE:
        g_helpVisible = false;
        ShowWindow(hwnd, SW_HIDE);
        SavePos();
        return 0;
    case WM_DESTROY:
        if (g_font) { DeleteObject(g_font); g_font = NULL; }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void CreateHelpWindow() {
    int h = 8 + kLineH * (int)Lines(false).size();
    int x, y; bool wasVisible;
    LoadPos(x, y, wasVisible);
    if (x < 0 || y < 0) {
        x = GetSystemMetrics(SM_CXSCREEN) - kW - 20;
        y = GetSystemMetrics(SM_CYSCREEN) - h - 60;
    }
    g_helpWindow = CreateOverlayWindow(L"VimouseHelp", HelpWndProc, WS_EX_TOPMOST | WS_EX_TOOLWINDOW, x, y, kW, h, 190);
    if (g_helpWindow) {
        g_helpVisible = wasVisible;
        ShowWindow(g_helpWindow, wasVisible ? SW_SHOWNA : SW_HIDE);
    }
}

void ToggleHelpWindow() {
    if (!g_helpWindow) return;
    g_helpVisible = !g_helpVisible;
    if (g_helpVisible) {
        InvalidateRect(g_helpWindow, NULL, TRUE);
        SetWindowPos(g_helpWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        ShowWindow(g_helpWindow, SW_HIDE);
    }
    SavePos();
}

void ShowHelpDialog(HWND owner) {
    bool zh = IsSystemChinese();
    std::wstring text;
    for (const auto& l : Lines(zh)) text += l + L"\n";
    text += zh ? L"\n托盘菜单 → 设置 可修改所有快捷键。\n管道 IPC: \\\\.\\pipe\\vimouse（Vimouse.exe -c help 查看命令）"
               : L"\nTray menu → Settings to rebind every key.\nPipe IPC: \\\\.\\pipe\\vimouse (Vimouse.exe -c help)";
    MessageBoxW(owner, text.c_str(), zh ? L"Vimouse 操作指南" : L"Vimouse Quick Guide", MB_OK | MB_ICONINFORMATION);
}
