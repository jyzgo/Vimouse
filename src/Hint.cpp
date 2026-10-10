#include "Hint.h"
#include "State.h"
#include "Overlay.h"
#include "Screens.h"
#include "Grid.h"
#include "Indicator.h"
#include "KeyOsd.h"

static const int N = 26;
static HFONT  g_font = NULL;
static int    g_fontH = 0;
static HBRUSH g_dark = NULL, g_light = NULL;

RECT HintCellRect(const RECT& a, int col, int row) {
    int w = RectW(a), h = RectH(a);
    return { a.left + col * w / N, a.top + row * h / N, a.left + (col + 1) * w / N, a.top + (row + 1) * h / N };
}

POINT HintCellCenter(const RECT& a, int col, int row) {
    return RectCenter(HintCellRect(a, col, row));
}

static LRESULT CALLBACK HintWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        DoubleBuffer db(hwnd);
        int fh = max(8, db.Height() / N / 2);
        if (!g_font || g_fontH != fh) {
            if (g_font) DeleteObject(g_font);
            g_font = MakeFont(fh, FW_BOLD, L"Arial");
            g_fontH = fh;
        }
        if (!g_dark)  g_dark = CreateSolidBrush(RGB(15, 15, 25));
        if (!g_light) g_light = CreateSolidBrush(RGB(35, 35, 50));

        HGDIOBJ old = SelectObject(db.mem, g_font);
        int filterCol = (g_currentHint.size() == 1) ? g_currentHint[0] - 'A' : -1;
        for (int row = 0; row < N; row++) {
            for (int col = 0; col < N; col++) {
                RECT cell = HintCellRect(db.rc, col, row);
                FillRect(db.mem, &cell, ((row + col) & 1) ? g_light : g_dark);
                if (filterCol >= 0 && col != filterCol) continue;
                // 只改字色：列号（第一个字母）黄色，行号（第二个字母）白色
                char s[2] = { (char)('A' + col), (char)('A' + row) };
                SIZE all, first;
                GetTextExtentPoint32A(db.mem, s, 2, &all);
                GetTextExtentPoint32A(db.mem, s, 1, &first);
                const int tx = (cell.left + cell.right - all.cx) / 2, ty = (cell.top + cell.bottom - all.cy) / 2;
                SetTextColor(db.mem, RGB(255, 214, 0));
                TextOutA(db.mem, tx, ty, &s[0], 1);
                SetTextColor(db.mem, RGB(255, 255, 255));
                TextOutA(db.mem, tx + first.cx, ty, &s[1], 1);
            }
        }
        SelectObject(db.mem, old);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        // 点击格子 → 跳到格子中心
        RECT rc; GetClientRect(hwnd, &rc);
        int x = (short)LOWORD(lParam), y = (short)HIWORD(lParam);
        int c = x * N / max(1, RectW(rc)), r = y * N / max(1, RectH(rc));
        RECT sr = ScreenRectAt(g_hintScreenIndex);
        POINT p = HintCellCenter(sr, max(0, min(N - 1, c)), max(0, min(N - 1, r)));
        SetCursorPos(p.x, p.y);
        ExitHintMode(false);
        KeyOsd_CursorPulse(true, 200);
        return 0;
    }
    case WM_RBUTTONDOWN:
        ExitHintMode(false);
        return 0;
    case WM_DESTROY:
        if (g_font)  { DeleteObject(g_font);  g_font = NULL; }
        if (g_dark)  { DeleteObject(g_dark);  g_dark = NULL; }
        if (g_light) { DeleteObject(g_light); g_light = NULL; }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void CreateHintWindow() {
    g_hintWindow = CreateOverlayWindow(L"VimouseHint", HintWndProc,
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, 0, 0, 0, 0, 100);
}

void EnterHintMode() {
    if (g_hintMode || !g_hintWindow) return;
    g_hintMode = true;
    g_currentHint.clear();
    g_hintScreenIndex = GetCurrentScreenIndex();
    RECT sr = ScreenRectAt(g_hintScreenIndex);
    MoveWindow(g_hintWindow, sr.left, sr.top, RectW(sr), RectH(sr), TRUE);
    ShowWindow(g_hintWindow, SW_SHOWNA);
    InvalidateRect(g_hintWindow, NULL, TRUE);
    UpdateWindow(g_hintWindow);
    if (g_indicatorWindow) ShowWindow(g_indicatorWindow, SW_HIDE);
}

void ExitHintMode(bool showMiniGrid) {
    if (!g_hintMode) return;
    g_hintMode = false;
    g_currentHint.clear();
    ShowWindow(g_hintWindow, SW_HIDE);
    g_mouseSpeed = g_lastSetSpeed = 15;   // Hint 之后微调，起步速度略快
    UpdateIndicatorPosition();
    if (showMiniGrid) EnterMiniGrid();
}

void HintTypeLetter(char letter) {
    if (!g_hintMode) return;
    if (g_currentHint.empty()) {
        g_currentHint += letter;
        InvalidateRect(g_hintWindow, NULL, TRUE);
        return;
    }
    g_currentHint += letter;
    RECT sr = ScreenRectAt(g_hintScreenIndex);
    POINT p = HintCellCenter(sr, g_currentHint[0] - 'A', g_currentHint[1] - 'A');
    SetCursorPos(p.x, p.y);
    ExitHintMode(true);
    KeyOsd_CursorPulse(true, 200);   // 选中格子：光标处放一次短脉冲（200ms）
}
