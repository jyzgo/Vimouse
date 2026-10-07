#include "Grid.h"
#include "State.h"
#include "Overlay.h"
#include "Screens.h"
#include "Keymap.h"
#include "Indicator.h"

static HPEN  g_crossPen = NULL, g_diagPen = NULL;
static HFONT g_labelFont = NULL;
static int   g_labelSize = 0;
static int   g_gridPad = 0;   // 小区域时窗口向外扩出的边距（标签画在方块外面），0 = 不扩

static const int kMinInnerFont = 22;   // 方块内标签低于该字号时改为外置标签
static const int kOuterFont = 26;      // 外置标签字号

static void EnsureRes(int fontSize) {
    if (!g_crossPen) g_crossPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    if (!g_diagPen)  g_diagPen = CreatePen(PS_SOLID, 1, RGB(120, 120, 120));
    if (!g_labelFont || g_labelSize != fontSize) {
        if (g_labelFont) DeleteObject(g_labelFont);
        g_labelFont = MakeFont(fontSize, FW_BOLD, L"Consolas", FIXED_PITCH | FF_MODERN);
        g_labelSize = fontSize;
    }
}

static POINT SplitPoint(const RECT& cur) {
    if (!g_gridCustomCenter) return RectCenter(cur);
    return { max(cur.left, min(cur.right, g_gridCenter.x)), max(cur.top, min(cur.bottom, g_gridCenter.y)) };
}

static LRESULT CALLBACK GridWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        DoubleBuffer db(hwnd);
        const int pad = g_gridPad;
        // inner = 实际选择区域（窗口坐标）；有边距时四周留给标签
        const int x0 = pad, y0 = pad;
        const int w = max(2, db.Width() - 2 * pad), h = max(2, db.Height() - 2 * pad);
        int midX = w / 2, midY = h / 2;
        if (g_gridCustomCenter && !g_gridStack.empty()) {
            const RECT& sr = g_gridStack.back();
            midX = max(1, min(w - 1, (int)(g_gridCenter.x - sr.left)));
            midY = max(1, min(h - 1, (int)(g_gridCenter.y - sr.top)));
        }
        int fontSize = pad ? kOuterFont : max(kMinInnerFont, min(40, min(w, h) / 6));
        EnsureRes(fontSize);

        FillRect(db.mem, &db.rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        HGDIOBJ oldPen = SelectObject(db.mem, g_crossPen);
        if (pad) {   // 外置标签时画出选择方块边框
            HGDIOBJ oldBrush = SelectObject(db.mem, GetStockObject(NULL_BRUSH));
            Rectangle(db.mem, x0 - 1, y0 - 1, x0 + w + 1, y0 + h + 1);
            SelectObject(db.mem, oldBrush);
        }
        const int cx = x0 + midX, cy = y0 + midY;
        MoveToEx(db.mem, cx, y0, NULL); LineTo(db.mem, cx, y0 + h);
        MoveToEx(db.mem, x0, cy, NULL); LineTo(db.mem, x0 + w, cy);
        SelectObject(db.mem, g_diagPen);
        MoveToEx(db.mem, x0, y0, NULL);         LineTo(db.mem, cx, cy);
        MoveToEx(db.mem, x0 + w, y0, NULL);     LineTo(db.mem, cx, cy);
        MoveToEx(db.mem, x0, y0 + h, NULL);     LineTo(db.mem, cx, cy);
        MoveToEx(db.mem, x0 + w, y0 + h, NULL); LineTo(db.mem, cx, cy);
        SelectObject(db.mem, oldPen);

        // 方向键标签（读取当前键位）：大区域画在方块内靠近分割中心 75% 处；
        // 小区域（如 Hint 之后的微调方块）画在方块外一圈，字才能放大
        int lx, rx, ty, by;
        if (pad) {
            lx = pad / 2; rx = x0 + w + pad / 2;
            ty = pad / 2; by = y0 + h + pad / 2;
        } else {
            lx = x0 + midX * 3 / 4; rx = cx + (w - midX) / 4;
            ty = y0 + midY * 3 / 4; by = cy + (h - midY) / 4;
        }
        struct { Action a; int x, y; COLORREF c; } labels[] = {
            { Action::MoveLeft,      lx,   cy,   RGB(50, 255, 120) },
            { Action::MoveRight,     rx,   cy,   RGB(50, 255, 120) },
            { Action::MoveUp,        cx,   ty,   RGB(80, 200, 255) },
            { Action::MoveDown,      cx,   by,   RGB(80, 200, 255) },
            { Action::MoveUpLeft,    lx,   ty,   RGB(200, 200, 100) },
            { Action::MoveUpRight,   rx,   ty,   RGB(200, 200, 100) },
            { Action::MoveDownLeft,  lx,   by,   RGB(200, 200, 100) },
            { Action::MoveDownRight, rx,   by,   RGB(200, 200, 100) },
        };
        HGDIOBJ oldFont = SelectObject(db.mem, g_labelFont);
        for (const auto& lb : labels) {
            std::wstring key = VkName(g_keymap[(int)lb.a].vk);
            SIZE sz; GetTextExtentPoint32W(db.mem, key.c_str(), (int)key.size(), &sz);
            RECT tr = { lb.x - sz.cx / 2 - 3, lb.y - sz.cy / 2 - 1, lb.x + sz.cx / 2 + 3, lb.y + sz.cy / 2 + 1 };
            FillRect(db.mem, &tr, (HBRUSH)GetStockObject(BLACK_BRUSH));
            SetTextColor(db.mem, lb.c);
            DrawTextW(db.mem, key.c_str(), -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(db.mem, oldFont);
        return 0;
    }
    case WM_RBUTTONDOWN:
        ExitGridMode();
        return 0;
    case WM_DESTROY:
        if (g_crossPen)  { DeleteObject(g_crossPen);  g_crossPen = NULL; }
        if (g_diagPen)   { DeleteObject(g_diagPen);   g_diagPen = NULL; }
        if (g_labelFont) { DeleteObject(g_labelFont); g_labelFont = NULL; }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void CreateGridWindow() {
    g_gridWindow = CreateOverlayWindow(L"VimouseGrid", GridWndProc,
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, 0, 0, 0, 0, 150);
}

static void ShowRegion(const RECT& r) {
    if (!g_gridWindow) return;
    // 区域太小、方块内放不下大字时，窗口向外扩一圈，把标签画在方块外面
    g_gridPad = (min(RectW(r), RectH(r)) / 6 < kMinInnerFont) ? kOuterFont * 3 / 2 + 4 : 0;
    const int p = g_gridPad;
    MoveWindow(g_gridWindow, r.left - p, r.top - p, RectW(r) + 2 * p, RectH(r) + 2 * p, TRUE);
    ShowWindow(g_gridWindow, SW_SHOWNA);
    InvalidateRect(g_gridWindow, NULL, TRUE);
    UpdateWindow(g_gridWindow);
    if (g_indicatorWindow) ShowWindow(g_indicatorWindow, SW_HIDE);
}

static void BeginGrid(const RECT& region, bool mini) {
    g_gridMode = true;
    g_miniGridMode = mini;
    g_gridStack.clear();
    g_gridStack.push_back(region);
    ShowRegion(region);
}

void EnterGridMode() {
    if (g_gridMode) return;
    RECT sr = ScreenRectAt(GetCurrentScreenIndex());
    g_gridCustomCenter = false;
    POINT c = RectCenter(sr);
    SetCursorPos(c.x, c.y);
    BeginGrid(sr, false);
}

void EnterGridModeAtCursor() {
    if (g_gridMode) return;
    RECT sr = ScreenRectAt(GetCurrentScreenIndex());
    GetCursorPos(&g_gridCenter);
    g_gridCustomCenter = true;
    BeginGrid(sr, false);
}

void EnterMiniGrid() {
    if (g_gridMode) return;
    RECT sr = ScreenRectAt(GetCurrentScreenIndex());
    int cw = RectW(sr) / 26, ch = RectH(sr) / 26;
    POINT p; GetCursorPos(&p);
    RECT region = { p.x - cw / 2, p.y - ch / 2, p.x + cw / 2, p.y + ch / 2 };
    g_gridCustomCenter = false;
    BeginGrid(region, true);
}

void ExitGridMode() {
    if (!g_gridMode) return;
    g_gridMode = false;
    g_miniGridMode = false;
    g_gridCustomCenter = false;
    g_gridStack.clear();
    if (g_gridWindow) ShowWindow(g_gridWindow, SW_HIDE);
    UpdateIndicatorPosition();
}

void GridSelect(unsigned moveBit) {
    if (g_gridStack.empty()) return;
    RECT cur = g_gridStack.back();
    POINT mid = SplitPoint(cur);
    g_gridCustomCenter = false;   // 自定义中心只用于第一次分割

    RECT n = cur;
    switch (moveBit) {
    case MV_LEFT:      n.right = mid.x; break;
    case MV_RIGHT:     n.left = mid.x; break;
    case MV_UP:        n.bottom = mid.y; break;
    case MV_DOWN:      n.top = mid.y; break;
    case MV_UPLEFT:    n.right = mid.x; n.bottom = mid.y; break;
    case MV_UPRIGHT:   n.left = mid.x;  n.bottom = mid.y; break;
    case MV_DOWNLEFT:  n.right = mid.x; n.top = mid.y; break;
    case MV_DOWNRIGHT: n.left = mid.x;  n.top = mid.y; break;
    default: return;
    }
    POINT c = RectCenter(n);
    SetCursorPos(c.x, c.y);
    g_gridStack.push_back(n);
    ShowRegion(n);
}

void GridBack() {
    if (g_gridStack.size() <= 1) { ExitGridMode(); return; }
    g_gridStack.pop_back();
    const RECT& prev = g_gridStack.back();
    POINT c = RectCenter(prev);
    SetCursorPos(c.x, c.y);
    ShowRegion(prev);
}
