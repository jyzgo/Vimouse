#include "Hint.h"
#include "State.h"
#include "Overlay.h"
#include "Screens.h"
#include "Grid.h"
#include "Indicator.h"
#include <vector>

static const int N = 26;
static HFONT  g_font = NULL;
static int    g_fontH = 0;
static HBRUSH g_dark = NULL, g_light = NULL;

// 窗口不透明，画「压暗的屏幕快照」当蒙版（跟原来半透明格子的观感一致），字母加黑色实心描边：
// 默认：列号（第一个字母）黄色、行号白色；选中一列后：只剩那一列，两个字母都黄色。
// 快照在 Hint 窗口显示之前截，里面是干净的屏幕。
static HBITMAP g_snap = NULL;
static DWORD*  g_snapBits = nullptr;
static int     g_snapW = 0, g_snapH = 0;
static HFONT   g_maskFont = NULL;                     // 遮罩用灰度抗锯齿字体（ClearType 会串色）
static int     g_maskFontH = 0;
static const BYTE     kWndAlpha  = 100;               // 没有快照时的退路：整窗半透明（原样）
static const COLORREF kFocusText = RGB(255, 214, 0);  // 列号 / 选中列字母

static HBITMAP MakeDib(HDC ref, int w, int h, DWORD** bits) {
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // 自上而下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* p = nullptr;
    HBITMAP b = CreateDIBSection(ref, &bi, DIB_RGB_COLORS, &p, NULL, 0);
    if (b && !p) { DeleteObject(b); b = NULL; }
    *bits = (DWORD*)p;
    return b;
}

static void FreeSnapshot() {
    if (g_snap) { DeleteObject(g_snap); g_snap = NULL; }
    g_snapBits = nullptr;
    g_snapW = g_snapH = 0;
}

static void TakeSnapshot(const RECT& sr) {
    FreeSnapshot();
    const int w = RectW(sr), h = RectH(sr);
    if (w <= 0 || h <= 0) return;
    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    DWORD* bits = nullptr;
    g_snap = MakeDib(screen, w, h, &bits);
    if (g_snap) {
        HGDIOBJ old = SelectObject(mem, g_snap);
        BitBlt(mem, 0, 0, w, h, screen, sr.left, sr.top, SRCCOPY | CAPTUREBLT);
        SelectObject(mem, old);
        GdiFlush();
        g_snapBits = bits;
        g_snapW = w; g_snapH = h;
    }
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

// 整屏画面：蒙版 = 快照 × 61% + 原来的深色格子 × 39%（等同 alpha 100 叠加的观感），
// 再画字母 + 2px 黑色实心描边。focusCol < 0 = 全部列（列号黄、行号白）；否则只画那一列（全黄）
static void PaintFocusColumn(HDC dst, const RECT& rc, int focusCol) {
    const int w = RectW(rc), h = RectH(rc);
    DWORD *out = nullptr, *mask = nullptr;
    HBITMAP obmp = MakeDib(dst, w, h, &out);
    HBITMAP mbmp = MakeDib(dst, w, h, &mask);   // 初始全 0
    if (!obmp || !mbmp) {
        if (obmp) DeleteObject(obmp);
        if (mbmp) DeleteObject(mbmp);
        return;
    }
    const int a = kWndAlpha;
    for (int row = 0; row < N; row++)
        for (int col = 0; col < N; col++) {
            RECT c = HintCellRect(rc, col, row);
            // 原来的深色格子 RGB(35,35,50) / RGB(15,15,25)，按 shift 0/8/16 = B/G/R
            const bool lightCell = ((row + col) & 1) != 0;
            const int bgc[3] = { lightCell ? 50 : 25, lightCell ? 35 : 15, lightCell ? 35 : 15 };
            for (int y = max(0L, c.top); y < min((LONG)h, c.bottom); y++)
                for (int x = max(0L, c.left); x < min((LONG)w, c.right); x++) {
                    size_t i = (size_t)y * w + x;
                    DWORD p = g_snapBits[i], v = 0;
                    for (int k = 0; k < 3; k++) {
                        int s = (p >> (k * 8)) & 0xFF;
                        v |= (DWORD)((s * (255 - a) + bgc[k] * a) / 255) << (k * 8);
                    }
                    out[i] = v;
                }
        }

    // 字母先画进遮罩：第一个字母画在红通道、第二个画在绿通道（灰度抗锯齿，通道值 = 覆盖度）
    const int fh = max(8, h / N / 2);
    if (!g_maskFont || g_maskFontH != fh) {
        if (g_maskFont) DeleteObject(g_maskFont);
        g_maskFont = CreateFontW(fh, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
        g_maskFontH = fh;
    }
    HDC mdc = CreateCompatibleDC(dst);
    HGDIOBJ oldM = SelectObject(mdc, mbmp);
    HGDIOBJ oldF = SelectObject(mdc, g_maskFont);
    SetBkMode(mdc, TRANSPARENT);
    for (int col = 0; col < N; col++) {
        if (focusCol >= 0 && col != focusCol) continue;
        for (int row = 0; row < N; row++) {
            RECT cell = HintCellRect(rc, col, row);
            char s[2] = { (char)('A' + col), (char)('A' + row) };
            SIZE all, first;
            GetTextExtentPoint32A(mdc, s, 2, &all);
            GetTextExtentPoint32A(mdc, s, 1, &first);
            const int tx = (cell.left + cell.right - all.cx) / 2, ty = (cell.top + cell.bottom - all.cy) / 2;
            SetTextColor(mdc, RGB(255, 0, 0));
            TextOutA(mdc, tx, ty, &s[0], 1);
            SetTextColor(mdc, RGB(0, 255, 0));
            TextOutA(mdc, tx + first.cx, ty, &s[1], 1);
        }
    }
    SelectObject(mdc, oldF);
    SelectObject(mdc, oldM);
    DeleteDC(mdc);
    GdiFlush();

    // 合成：字形外扩 2px 是黑色描边；第一个字母黄色，第二个字母白色（选中列时也是黄色）
    RECT colR = focusCol >= 0 ? HintCellRect(rc, focusCol, 0) : rc;
    const int x0 = max(0L, colR.left), x1 = min((LONG)w, colR.right), bw = x1 - x0;
    if (bw > 0) {
        const int R = 2;
        std::vector<BYTE> m((size_t)bw * h), ca((size_t)bw * h), cb((size_t)bw * h), tmp((size_t)bw * h);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < bw; x++) {
                const DWORD q = mask[(size_t)y * w + x0 + x];
                const size_t j = (size_t)y * bw + x;
                ca[j] = (BYTE)((q >> 16) & 0xFF);
                cb[j] = (BYTE)((q >> 8) & 0xFF);
                m[j] = max(ca[j], cb[j]);
            }
        for (int y = 0; y < h; y++)
            for (int x = 0; x < bw; x++) {
                BYTE v = 0;
                for (int k = max(0, x - R); k <= min(bw - 1, x + R); k++) v = max(v, m[(size_t)y * bw + k]);
                tmp[(size_t)y * bw + x] = v;
            }
        const COLORREF second = focusCol >= 0 ? kFocusText : RGB(255, 255, 255);
        const int fa[3] = { GetBValue(kFocusText), GetGValue(kFocusText), GetRValue(kFocusText) };
        const int fb[3] = { GetBValue(second), GetGValue(second), GetRValue(second) };
        for (int y = 0; y < h; y++)
            for (int x = 0; x < bw; x++) {
                bool halo = false;
                for (int k = max(0, y - R); k <= min(h - 1, y + R) && !halo; k++) halo = tmp[(size_t)k * bw + x] != 0;
                if (!halo) continue;
                const size_t j = (size_t)y * bw + x;
                const int a1 = ca[j], b1 = cb[j];
                DWORD v = 0;
                for (int c = 0; c < 3; c++) v |= (DWORD)min(255, (fa[c] * a1 + fb[c] * b1) / 255) << (c * 8);
                out[(size_t)y * w + x0 + x] = v;
            }
    }
    DeleteObject(mbmp);

    HDC odc = CreateCompatibleDC(dst);
    HGDIOBJ oldO = SelectObject(odc, obmp);
    BitBlt(dst, 0, 0, w, h, odc, 0, 0, SRCCOPY);
    SelectObject(odc, oldO);
    DeleteDC(odc);
    DeleteObject(obmp);
}

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

        int filterCol = (g_currentHint.size() == 1) ? g_currentHint[0] - 'A' : -1;
        if (filterCol >= N) filterCol = -1;
        const bool focus = g_snapBits && g_snapW == db.Width() && g_snapH == db.Height();
        SetLayeredWindowAttributes(hwnd, 0, focus ? 255 : kWndAlpha, LWA_ALPHA);
        if (focus) {
            PaintFocusColumn(db.mem, db.rc, filterCol);
            return 0;
        }
        HGDIOBJ old = SelectObject(db.mem, g_font);
        SetTextColor(db.mem, RGB(255, 255, 255));
        for (int row = 0; row < N; row++) {
            for (int col = 0; col < N; col++) {
                RECT cell = HintCellRect(db.rc, col, row);
                FillRect(db.mem, &cell, ((row + col) & 1) ? g_light : g_dark);
                if (filterCol >= 0 && col != filterCol) continue;
                char s[3] = { (char)('A' + col), (char)('A' + row), 0 };
                DrawTextA(db.mem, s, 2, &cell, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
        return 0;
    }
    case WM_RBUTTONDOWN:
        ExitHintMode(false);
        return 0;
    case WM_DESTROY:
        FreeSnapshot();
        if (g_maskFont) { DeleteObject(g_maskFont); g_maskFont = NULL; }
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
    if (g_indicatorWindow) ShowWindow(g_indicatorWindow, SW_HIDE);   // 先藏起来，免得被截进快照
    TakeSnapshot(sr);   // Hint 窗口显示之前截：选中一列后用它当蒙版底图
    SetLayeredWindowAttributes(g_hintWindow, 0, kWndAlpha, LWA_ALPHA);
    MoveWindow(g_hintWindow, sr.left, sr.top, RectW(sr), RectH(sr), TRUE);
    ShowWindow(g_hintWindow, SW_SHOWNA);
    InvalidateRect(g_hintWindow, NULL, TRUE);
    UpdateWindow(g_hintWindow);
}

void ExitHintMode(bool showMiniGrid) {
    if (!g_hintMode) return;
    g_hintMode = false;
    g_currentHint.clear();
    ShowWindow(g_hintWindow, SW_HIDE);
    FreeSnapshot();
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
}
