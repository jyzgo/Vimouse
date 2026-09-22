#include "Cursor.h"
#include "State.h"
#include <windows.h>
#include <cmath>
#include <cstring>

#ifndef OCR_NORMAL
#define OCR_NORMAL 32512
#endif

static HCURSOR g_cursorIdle = NULL;
static HCURSOR g_cursorMoving = NULL;
static bool    g_cursorApplied = false;

// 32x32 准星：四条短臂 + 中心空心圆环，带 1px 半透明暗边。
// 中心不再是实心色块，不遮挡目标像素。
static HCURSOR CreateCrosshairCursor(COLORREF line, COLORREF ring) {
    const int size = 32;
    const int hot = 15;

    HDC screenDC = GetDC(NULL);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    DWORD* px = nullptr;
    HBITMAP hColor = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, (void**)&px, NULL, 0);
    if (!hColor || !px) { ReleaseDC(NULL, screenDC); return NULL; }
    memset(px, 0, size * size * 4);

    auto put = [&](int x, int y, COLORREF c, BYTE a) {
        if (x < 0 || x >= size || y < 0 || y >= size) return;
        px[y * size + x] = ((DWORD)a << 24) | ((DWORD)GetRValue(c) << 16) | ((DWORD)GetGValue(c) << 8) | GetBValue(c);
    };
    auto putIfEmpty = [&](int x, int y, COLORREF c, BYTE a) {
        if (x < 0 || x >= size || y < 0 || y >= size) return;
        if ((px[y * size + x] >> 24) == 0) put(x, y, c, a);
    };

    const COLORREF shadow = RGB(0, 0, 0);
    const int armIn = 5, armOut = 12;

    // 四条臂
    for (int d = armIn; d <= armOut; d++) {
        put(hot + d, hot, line, 255); put(hot - d, hot, line, 255);
        put(hot, hot + d, line, 255); put(hot, hot - d, line, 255);
    }
    // 臂的暗边
    for (int d = armIn - 1; d <= armOut + 1; d++) {
        for (int s = -1; s <= 1; s += 2) {
            putIfEmpty(hot + d, hot + s, shadow, 120); putIfEmpty(hot - d, hot + s, shadow, 120);
            putIfEmpty(hot + s, hot + d, shadow, 120); putIfEmpty(hot + s, hot - d, shadow, 120);
        }
    }
    putIfEmpty(hot + armOut + 1, hot, shadow, 120); putIfEmpty(hot - armOut - 1, hot, shadow, 120);
    putIfEmpty(hot, hot + armOut + 1, shadow, 120); putIfEmpty(hot, hot - armOut - 1, shadow, 120);

    // 中心圆环 r≈2.5，环外 1px 暗边
    for (int y = -4; y <= 4; y++) {
        for (int x = -4; x <= 4; x++) {
            double r = std::sqrt((double)(x * x + y * y));
            if (r >= 1.8 && r <= 3.1)       put(hot + x, hot + y, ring, 255);
            else if (r > 3.1 && r <= 4.1)   putIfEmpty(hot + x, hot + y, shadow, 110);
        }
    }

    // 单色掩码：有像素的地方置 0
    HBITMAP hMask = CreateBitmap(size, size, 1, 1, NULL);
    HDC maskDC = CreateCompatibleDC(screenDC);
    HGDIOBJ oldBmp = SelectObject(maskDC, hMask);
    PatBlt(maskDC, 0, 0, size, size, WHITENESS);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
            if ((px[y * size + x] >> 24) > 0) SetPixel(maskDC, x, y, RGB(0, 0, 0));
    SelectObject(maskDC, oldBmp);
    DeleteDC(maskDC);

    ICONINFO ii = {};
    ii.fIcon = FALSE;
    ii.xHotspot = hot;
    ii.yHotspot = hot;
    ii.hbmMask = hMask;
    ii.hbmColor = hColor;
    HCURSOR hCur = (HCURSOR)CreateIconIndirect(&ii);

    DeleteObject(hColor);
    DeleteObject(hMask);
    ReleaseDC(NULL, screenDC);
    return hCur;
}

static void EnsureCursors() {
    if (!g_cursorIdle)   g_cursorIdle = CreateCrosshairCursor(RGB(0, 220, 120), RGB(255, 255, 255));
    if (!g_cursorMoving) g_cursorMoving = CreateCrosshairCursor(RGB(255, 160, 0), RGB(255, 255, 255));
}

static void ApplyCursor(HCURSOR cursor) {
    if (!cursor) return;
    // SetSystemCursor 会接管并销毁传入句柄，必须传副本
    HCURSOR copy = CopyCursor(cursor);
    if (copy) {
        SetSystemCursor(copy, OCR_NORMAL);
        g_cursorApplied = true;
    }
}

void SetVimouseCursor() {
    if (!g_settings.customCursor) return;
    EnsureCursors();
    ApplyCursor(g_cursorIdle);
}

void SetMovingCursor() {
    if (!g_settings.customCursor) return;
    EnsureCursors();
    ApplyCursor(g_cursorMoving);
}

void RestoreSystemCursor() {
    if (!g_cursorApplied) return;
    SystemParametersInfo(SPI_SETCURSORS, 0, NULL, 0);
    g_cursorApplied = false;
}

void DestroyCustomCursors() {
    RestoreSystemCursor();
    if (g_cursorIdle)   { DestroyCursor(g_cursorIdle);   g_cursorIdle = NULL; }
    if (g_cursorMoving) { DestroyCursor(g_cursorMoving); g_cursorMoving = NULL; }
}
