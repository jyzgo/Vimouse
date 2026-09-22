#include "Indicator.h"
#include "State.h"
#include "Overlay.h"
#include "Screens.h"

#define TIMER_CLICK_FLASH 1001

static HFONT  g_font = NULL;
static HFONT  g_fontBig = NULL;

// 光标所在 hint 格子的两字母坐标（列, 行）
static void HintCoordAt(POINT p, char out[3]) {
    out[0] = '?'; out[1] = '?'; out[2] = 0;
    for (const RECT& sr : g_screenRects) {
        if (!PtInRect(&sr, p)) continue;
        int col = (p.x - sr.left) * 26 / RectW(sr);
        int row = (p.y - sr.top) * 26 / RectH(sr);
        out[0] = (char)('A' + max(0, min(25, col)));
        out[1] = (char)('A' + max(0, min(25, row)));
        return;
    }
}

static LRESULT CALLBACK IndicatorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        DoubleBuffer db(hwnd);
        if (!g_font)    g_font = MakeFont(14, FW_BOLD, L"Consolas", FIXED_PITCH | FF_MODERN);
        if (!g_fontBig) g_fontBig = MakeFont(20, FW_BOLD, L"Consolas", FIXED_PITCH | FF_MODERN);
        FillRect(db.mem, &db.rc, (HBRUSH)GetStockObject(BLACK_BRUSH));

        if (g_remoteMode) {
            HGDIOBJ old = SelectObject(db.mem, g_font);
            RECT rl = db.rc; rl.right -= 18;
            SetTextColor(db.mem, RGB(100, 200, 255));
            DrawTextA(db.mem, "RMT", 3, &rl, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (g_remoteLastKey[0]) {
                RECT rk = db.rc; rk.left = db.rc.right - 18;
                SetTextColor(db.mem, RGB(255, 230, 50));
                DrawTextA(db.mem, g_remoteLastKey, -1, &rk, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            SelectObject(db.mem, old);
        } else {
            POINT p; GetCursorPos(&p);
            char coord[3]; HintCoordAt(p, coord);
            HGDIOBJ old = SelectObject(db.mem, g_clickFlash ? g_fontBig : g_font);
            if (g_clickFlash) {
                SetTextColor(db.mem, RGB(255, 230, 50));
                DrawTextA(db.mem, coord, 2, &db.rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else {
                RECT r1 = db.rc; r1.right = db.Width() / 2;
                RECT r2 = db.rc; r2.left = r1.right;
                SetTextColor(db.mem, RGB(50, 255, 120));
                DrawTextA(db.mem, coord, 1, &r1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SetTextColor(db.mem, RGB(255, 160, 40));
                DrawTextA(db.mem, coord + 1, 1, &r2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            SelectObject(db.mem, old);
        }
        return 0;
    }
    case WM_TIMER:
        if (wParam == TIMER_CLICK_FLASH) {
            KillTimer(hwnd, TIMER_CLICK_FLASH);
            EndClickFlash();
        }
        return 0;
    case WM_DESTROY:
        if (g_font)    { DeleteObject(g_font);    g_font = NULL; }
        if (g_fontBig) { DeleteObject(g_fontBig); g_fontBig = NULL; }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void CreateIndicatorWindow() {
    g_indicatorWindow = CreateOverlayWindow(L"VimouseIndicator", IndicatorWndProc,
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        0, 0, 22, 16, 200);
}

void RefreshIndicator() {
    if (g_indicatorWindow) InvalidateRect(g_indicatorWindow, NULL, TRUE);
}

void UpdateIndicatorPosition() {
    if (!g_indicatorWindow) return;
    bool show = g_isActive && !g_hintMode && !g_gridMode;
    if (!show) { ShowWindow(g_indicatorWindow, SW_HIDE); return; }

    POINT p; GetCursorPos(&p);
    if (g_clickFlash) {
        RefreshIndicator();
    } else {
        int w = g_remoteMode ? 50 : 22;
        SetWindowPos(g_indicatorWindow, HWND_TOPMOST, p.x + 12, p.y + 12, w, 16,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        RefreshIndicator();
    }
}

void TriggerClickFlash(bool autoEnd) {
    if (!g_indicatorWindow) return;
    g_clickFlash = true;
    POINT p; GetCursorPos(&p);
    SetWindowPos(g_indicatorWindow, HWND_TOPMOST, p.x + 8, p.y + 8, 30, 22, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    RefreshIndicator();
    if (autoEnd) SetTimer(g_indicatorWindow, TIMER_CLICK_FLASH, 300, NULL);
}

void EndClickFlash() {
    if (!g_indicatorWindow) return;
    g_clickFlash = false;
    UpdateIndicatorPosition();
}
