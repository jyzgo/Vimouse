#include "PointPeek.h"
#include "Overlay.h"
#include <string>

static const int kD = 28;                 // 圆直径
static HWND  s_wnd[PEEK_MAX] = {};
static HFONT s_font = NULL;

static LRESULT CALLBACK PeekWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        DoubleBuffer db(hwnd);
        if (!s_font) s_font = MakeFont(18, FW_BOLD, L"Consolas", FIXED_PITCH | FF_MODERN);
        const int idx = (int)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        // 0 = 松开会跳到的点：橙色；其余：青色
        COLORREF fill = idx == 0 ? RGB(255, 140, 0) : RGB(0, 150, 170);
        HBRUSH br = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        HGDIOBJ ob = SelectObject(db.mem, br), op = SelectObject(db.mem, pen);
        Ellipse(db.mem, 1, 1, kD - 1, kD - 1);
        SelectObject(db.mem, ob); SelectObject(db.mem, op);
        DeleteObject(br); DeleteObject(pen);

        HGDIOBJ of = SelectObject(db.mem, s_font);
        SetTextColor(db.mem, RGB(255, 255, 255));
        std::wstring s = std::to_wstring(idx);
        RECT r = { 0, 0, kD, kD };
        DrawTextW(db.mem, s.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(db.mem, of);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void PointPeek_Create() {
    for (int i = 0; i < PEEK_MAX; i++) {
        s_wnd[i] = CreateOverlayWindow(L"VimousePointPeek", PeekWndProc,
            WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            0, 0, kD, kD, 235);
        if (!s_wnd[i]) continue;
        SetWindowLongPtrW(s_wnd[i], GWLP_USERDATA, i);
        SetWindowRgn(s_wnd[i], CreateEllipticRgn(0, 0, kD + 1, kD + 1), FALSE);
    }
}

void PointPeek_Destroy() {
    for (HWND& h : s_wnd) if (h) { DestroyWindow(h); h = NULL; }
    if (s_font) { DeleteObject(s_font); s_font = NULL; }
}

void PointPeek_Show(const POINT* pts, int n) {
    // 倒序摆放，让 0 号压在最上面（点重叠时能看到 0）
    for (int i = PEEK_MAX - 1; i >= 0; i--) {
        if (!s_wnd[i]) continue;
        if (i >= n) { ShowWindow(s_wnd[i], SW_HIDE); continue; }
        SetWindowPos(s_wnd[i], HWND_TOPMOST, pts[i].x - kD / 2, pts[i].y - kD / 2, kD, kD,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(s_wnd[i], NULL, FALSE);
    }
}

void PointPeek_Hide() {
    for (HWND h : s_wnd) if (h) ShowWindow(h, SW_HIDE);
}
