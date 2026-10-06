#include "ModeHint.h"
#include "State.h"
#include "Config.h"
#include "Keymap.h"
#include "Overlay.h"
#include "Util.h"
#include <windows.h>
#include <algorithm>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

struct Row {
    std::wstring key, desc;   // key 为空 = 分区标题（desc 为标题文字）
    bool operator==(const Row& o) const { return key == o.key && desc == o.desc; }
};

struct Content {
    std::wstring title;
    COLORREF accent = 0;
    std::vector<Row> rows;
    bool operator==(const Content& o) const { return title == o.title && accent == o.accent && rows == o.rows; }
};

const UINT_PTR kTimerId = 1;
const int kPad = 10, kTitleH = 24, kLineH = 20, kSectionH = 28, kColGap = 14, kEdge = 16;
const BYTE kAlpha = 190;

HWND    g_wnd = NULL;
HFONT   g_font = NULL, g_bold = NULL;
Content g_cur;
RECT    g_curRect = {};
int     g_keyColW = 0;

std::wstring K(Action a) { return ChordToString(g_keymap[(int)a]); }

std::wstring Keys(std::initializer_list<Action> as) {
    std::wstring s;
    for (Action a : as) {
        if (!s.empty()) s += L"/";
        s += VkName(g_keymap[(int)a].vk);
    }
    return s;
}

// 按当前模式生成提示内容；返回 false 表示不显示
bool Build(Content& c) {
    if (!g_settings.modeHint) return false;
    if (g_modeHintSessionOff) return false;
    if (!g_isActive && !g_arrowMode) return false;

    const bool zh = IsSystemChinese();
    auto Z = [zh](const wchar_t* a, const wchar_t* b) { return std::wstring(zh ? a : b); };
    auto add = [&c](const std::wstring& k, const std::wstring& d) { c.rows.push_back({ k, d }); };
    auto section = [&c](const std::wstring& t) { c.rows.push_back({ L"", t }); };
    const std::wstring mv = Keys({ Action::MoveLeft, Action::MoveDown, Action::MoveUp, Action::MoveRight });
    const std::wstring dg = Keys({ Action::MoveUpLeft, Action::MoveUpRight, Action::MoveDownLeft, Action::MoveDownRight });
    const bool canRemote = g_remoteMode || !g_remoteHosts.empty();

    if (g_arrowMode) {
        c.title = Z(L"方向键模式", L"Arrow keys");
        c.accent = RGB(255, 120, 200);
        add(mv, L"← ↓ ↑ →");
        add(L"Shift+…", Z(L"选择", L"Select"));
        add(L"Ctrl+…", Z(L"按词移动", L"By word"));
        add(Z(L"其他键", L"Others"), Z(L"正常输入", L"Type normally"));
        section(Z(L"切换模式", L"Switch mode"));
        add(K(Action::Toggle), Z(L"→ 鼠标模式", L"→ Mouse mode"));
        add(K(Action::ToggleCenter), Z(L"→ 鼠标模式+居中", L"→ Mouse + center"));
        if (canRemote) add(K(Action::ToggleRemote), Z(L"→ 远程鼠标", L"→ Remote mouse"));
        add(K(Action::ToggleArrow), Z(L"退出方向键模式", L"Exit arrow mode"));
        add(K(Action::HideModeHint), Z(L"隐藏本提示框", L"Hide this panel"));
        return true;
    }

    if (g_gridMode) {
        c.title = g_miniGridMode ? Z(L"微调 Grid（选一次）", L"Fine grid (one pick)") : Z(L"Grid 定位", L"Grid");
        c.accent = RGB(80, 200, 255);
        add(mv, Z(L"选半区", L"Pick half"));
        add(dg, Z(L"选象限", L"Pick quarter"));
        add(K(Action::GridBack), Z(L"返回上级", L"Back"));
        add(K(Action::Grid), Z(L"从屏幕中心重来", L"Restart at center"));
        add(K(Action::ClickLeft), Z(L"左键", L"Left click"));
        add(K(Action::ClickRight), Z(L"右键", L"Right click"));
        add(K(Action::DragToggle), Z(L"拖拽", L"Drag"));
        add(L"Enter", Z(L"点击并退出", L"Click & exit"));
        add(L"Esc", Z(L"退出 Grid", L"Exit grid"));
    } else if (g_hintMode) {
        c.title = Z(L"Hint 跳转", L"Hint jump");
        c.accent = RGB(255, 210, 80);
        add(L"A-Z", Z(L"输入两个字母", L"Type 2 letters"));
        add(L"Esc", Z(L"取消", L"Cancel"));
    } else if (g_tagMode) {
        c.title = Z(L"标签跳转", L"Tag jump");
        c.accent = RGB(255, 160, 80);
        add(L"A-Z", Z(L"跳到该标签", L"Jump to tag"));
        add(K(Action::TagJump) + L"/Esc", Z(L"退出", L"Exit"));
    } else if (g_wheelMode) {
        c.title = Z(L"滚轮模式", L"Scroll mode");
        c.accent = RGB(170, 140, 255);
        add(mv, Z(L"左/下/上/右滚", L"Scroll"));
        add(K(Action::ClickLeft), Z(L"左键", L"Left click"));
        add(K(Action::ClickRight), Z(L"右键", L"Right click"));
        add(K(Action::DragToggle), Z(L"拖拽", L"Drag"));
        add(K(Action::Hint), Z(L"Hint 跳转", L"Hint jump"));
        add(K(Action::WheelMode) + L"/Esc", Z(L"退出滚轮", L"Exit scroll"));
        add(Z(L"其他键", L"Others"), Z(L"退出并照常输入", L"Exit & pass"));
    } else {
        c.title = Z(L"鼠标模式", L"Mouse mode");
        if (g_isDragging) c.title += Z(L" · 拖拽中", L" · dragging");
        c.accent = RGB(80, 255, 160);
        add(mv, Z(L"移动（Shift 精确）", L"Move (Shift=fine)"));
        add(dg, Z(L"对角移动", L"Diagonal"));
        add(K(Action::ClickLeft), Z(L"左键（可按住）", L"Left (hold)"));
        add(K(Action::ClickRight), Z(L"右键", L"Right click"));
        add(K(Action::ClickMiddle), Z(L"中键", L"Middle click"));
        add(K(Action::DragToggle), Z(L"拖拽开关", L"Drag toggle"));
        add(K(Action::Hint), Z(L"Hint 跳转", L"Hint jump"));
        add(K(Action::Grid), Z(L"Grid 定位", L"Grid"));
        add(K(Action::WheelMode), Z(L"滚轮模式", L"Scroll mode"));
        add(K(Action::ScreenCenter), Z(L"屏幕中心/切屏", L"Center/next screen"));
        add(K(Action::TagPut) + L"/" + K(Action::TagJump), Z(L"放/跳标签", L"Put/jump tag"));
        add(K(Action::HistPrev) + L"/" + K(Action::HistNext), Z(L"上/下一个位置", L"Prev/next pos"));
        add(L"Enter", Z(L"点击并退出", L"Click & exit"));
        add(L"Esc", Z(L"退出", L"Exit"));
    }

    section(Z(L"切换模式", L"Switch mode"));
    add(K(Action::ToggleArrow), Z(L"→ 方向键模式", L"→ Arrow keys"));
    if (canRemote) add(K(Action::ToggleRemote), g_remoteMode ? Z(L"断开远程", L"Disconnect remote") : Z(L"→ 远程鼠标", L"→ Remote mouse"));
    add(K(Action::Toggle), Z(L"关闭 Vimouse", L"Turn off"));
    add(K(Action::HideModeHint), Z(L"隐藏本提示框", L"Hide this panel"));

    if (g_remoteMode) c.title += L"  → " + Utf8ToWide(g_remoteHost);
    return true;
}

void EnsureFonts() {
    if (!g_font) g_font = MakeFont(16, FW_NORMAL, L"Microsoft YaHei UI");
    if (!g_bold) g_bold = MakeFont(17, FW_BOLD, L"Microsoft YaHei UI");
}

int TextW(HDC dc, HFONT f, const std::wstring& s) {
    HGDIOBJ old = SelectObject(dc, f);
    SIZE sz = {};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    SelectObject(dc, old);
    return sz.cx;
}

// 计算窗口矩形：光标所在显示器工作区的右侧、垂直居中
RECT Layout(const Content& c, int& keyColW) {
    EnsureFonts();
    HDC dc = GetDC(g_wnd);
    keyColW = 0;
    int descW = 0, headW = 0;
    for (const auto& r : c.rows) {
        if (r.key.empty()) { headW = (std::max)(headW, TextW(dc, g_bold, r.desc)); continue; }
        keyColW = (std::max)(keyColW, TextW(dc, g_font, r.key));
        descW = (std::max)(descW, TextW(dc, g_font, r.desc));
    }
    int titleW = TextW(dc, g_bold, c.title);
    ReleaseDC(g_wnd, dc);

    int w = kPad * 2 + (std::max)((std::max)(titleW, headW), keyColW + kColGap + descW);
    int h = kPad * 2 + kTitleH;
    for (const auto& r : c.rows) h += r.key.empty() ? kSectionH : kLineH;

    POINT p; GetCursorPos(&p);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT& wa = mi.rcWork;
    int x = wa.right - w - kEdge;
    int y = wa.top + ((wa.bottom - wa.top) - h) / 2;
    return { x, y, x + w, y + h };
}

void Tick() {
    Content c;
    if (!Build(c)) {
        if (IsWindowVisible(g_wnd)) ShowWindow(g_wnd, SW_HIDE);
        g_cur = Content();
        return;
    }
    int keyColW = 0;
    RECT r = Layout(c, keyColW);
    if (c == g_cur && EqualRect(&r, &g_curRect) && IsWindowVisible(g_wnd)) return;

    g_cur = c;
    g_curRect = r;
    g_keyColW = keyColW;
    // 每次内容变化都重新置顶，保证压在 Grid / Hint 全屏层之上
    SetWindowPos(g_wnd, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_wnd, NULL, FALSE);
}

void Paint(HWND hwnd) {
    DoubleBuffer db(hwnd);
    HBRUSH bg = CreateSolidBrush(RGB(18, 18, 26));
    FillRect(db.mem, &db.rc, bg);
    DeleteObject(bg);
    HBRUSH border = CreateSolidBrush(g_cur.accent);
    FrameRect(db.mem, &db.rc, border);
    DeleteObject(border);

    EnsureFonts();
    HGDIOBJ old = SelectObject(db.mem, g_bold);
    SetTextColor(db.mem, g_cur.accent);
    RECT tr = { kPad, kPad, db.rc.right - kPad, kPad + kTitleH };
    DrawTextW(db.mem, g_cur.title.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(db.mem, g_font);
    int top = kPad + kTitleH;
    for (const auto& row : g_cur.rows) {
        if (row.key.empty()) {
            // 分区标题：上方一条分隔线 + 灰色小标题
            int lineY = top + 5;
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(70, 70, 90));
            HGDIOBJ oldPen = SelectObject(db.mem, pen);
            MoveToEx(db.mem, kPad, lineY, NULL);
            LineTo(db.mem, db.rc.right - kPad, lineY);
            SelectObject(db.mem, oldPen);
            DeleteObject(pen);
            RECT hr = { kPad, top + 8, db.rc.right - kPad, top + kSectionH };
            SetTextColor(db.mem, RGB(140, 140, 165));
            DrawTextW(db.mem, row.desc.c_str(), -1, &hr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            top += kSectionH;
            continue;
        }
        RECT kr = { kPad, top, kPad + g_keyColW, top + kLineH };
        SetTextColor(db.mem, RGB(255, 220, 120));
        DrawTextW(db.mem, row.key.c_str(), -1, &kr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT dr = { kPad + g_keyColW + kColGap, top, db.rc.right - kPad, top + kLineH };
        SetTextColor(db.mem, RGB(200, 200, 215));
        DrawTextW(db.mem, row.desc.c_str(), -1, &dr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        top += kLineH;
    }
    SelectObject(db.mem, old);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER:
        if (wParam == kTimerId) Tick();
        return 0;
    case WM_PAINT:
        Paint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DESTROY:
        KillTimer(hwnd, kTimerId);
        if (g_font) { DeleteObject(g_font); g_font = NULL; }
        if (g_bold) { DeleteObject(g_bold); g_bold = NULL; }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

}  // namespace

void ModeHint_Create() {
    if (g_wnd) return;
    // WS_EX_TRANSPARENT + 分层窗口 = 鼠标点击直接穿透；NOACTIVATE 不抢焦点
    g_wnd = CreateOverlayWindow(L"VimouseModeHint", WndProc,
                                WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
                                0, 0, 10, 10, kAlpha);
    if (g_wnd) {
        SetTimer(g_wnd, kTimerId, 100, NULL);
        Tick();
    }
}

void ModeHint_Destroy() {
    if (g_wnd) { DestroyWindow(g_wnd); g_wnd = NULL; }
}
