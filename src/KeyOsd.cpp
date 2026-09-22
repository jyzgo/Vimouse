#include "KeyOsd.h"
#include "State.h"
#include "Overlay.h"
#include "Screens.h"
#include <vector>
#include <algorithm>

// 使用 UpdateLayeredWindow（逐像素 alpha）：圆角真正透明，整体 alpha 用于渐隐。

#define TIMER_FADE 1

namespace {

HWND  g_osd = NULL;
HFONT g_osdFont = NULL;
std::vector<WORD> g_held;      // 当前按住的非修饰键（按下顺序）
std::wstring g_text;
int   g_alpha = 0;
bool  g_fading = false;
const int kAlphaMax = 235;
const int kHeight = 60, kRadius = 14, kPadX = 26, kMinW = 72, kBottomGap = 72;

bool IsModifier(WORD vk) {
    switch (vk) {
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_LWIN: case VK_RWIN:
        return true;
    }
    return false;
}

Modifiers LiveMods() {
    return { (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0,
             (GetAsyncKeyState(VK_MENU) & 0x8000) != 0,
             (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 };
}

std::wstring BuildText(Modifiers m) {
    std::wstring mods;
    if (m.ctrl)  mods += L"Ctrl + ";
    if (m.alt)   mods += L"Alt + ";
    if (m.shift) mods += L"Shift + ";
    std::wstring keys;
    for (WORD vk : g_held) {
        if (!keys.empty()) keys += L"  ";
        keys += VkName(vk);
    }
    if (keys.empty()) {
        if (mods.size() >= 3) mods.erase(mods.size() - 3);   // 去掉末尾 " + "
        return mods;
    }
    return mods + keys;
}

// 像素 (x,y) 是否在圆角矩形内
bool InRoundRect(int x, int y, int w, int h, int r) {
    if (x < 0 || y < 0 || x >= w || y >= h) return false;
    int cx = (x < r) ? r : (x >= w - r) ? w - r - 1 : x;
    int cy = (y < r) ? r : (y >= h - r) ? h - r - 1 : y;
    if (cx == x || cy == y) return true;
    int dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

// 渲染到 32bpp DIB 并提交给 UpdateLayeredWindow
void Render(int alpha) {
    if (!g_osd) return;
    if (!g_osdFont) g_osdFont = MakeFont(30, FW_SEMIBOLD, L"Segoe UI");

    HDC screen = GetDC(NULL);
    HDC measure = CreateCompatibleDC(screen);
    HGDIOBJ oldF = SelectObject(measure, g_osdFont);
    SIZE sz = { 0, 0 };
    GetTextExtentPoint32W(measure, g_text.c_str(), (int)g_text.size(), &sz);
    SelectObject(measure, oldF);
    DeleteDC(measure);

    int w = max(kMinW, sz.cx + kPadX * 2), h = kHeight;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    DWORD* px = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, (void**)&px, NULL, 0);
    if (!bmp) { ReleaseDC(NULL, screen); return; }

    HDC mem = CreateCompatibleDC(screen);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    // 背景（整块填充，圆角外稍后清零）+ 细边
    HBRUSH bg = CreateSolidBrush(RGB(26, 28, 36));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(78, 84, 104));
    HGDIOBJ oldB = SelectObject(mem, bg), oldP = SelectObject(mem, pen);
    RECT full = { 0, 0, w, h };
    FillRect(mem, &full, bg);
    RoundRect(mem, 0, 0, w, h, kRadius * 2, kRadius * 2);
    SelectObject(mem, oldB); SelectObject(mem, oldP);
    DeleteObject(bg); DeleteObject(pen);

    // 文字
    oldF = SelectObject(mem, g_osdFont);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(238, 240, 246));
    DrawTextW(mem, g_text.c_str(), -1, &full, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(mem, oldF);
    GdiFlush();

    // 圆角内 alpha=255，外部 0（GDI 不写 alpha，这里统一补）
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            DWORD& p = px[y * w + x];
            p = InRoundRect(x, y, w, h, kRadius) ? (p | 0xFF000000) : 0;
        }

    // 放在光标所在显示器的工作区底部（工作区不含任务栏，避免被任务栏盖住）
    POINT cur; GetCursorPos(&cur);
    MONITORINFO mi = { sizeof(mi) };
    RECT area = ScreenRectAt(GetCurrentScreenIndex());
    if (GetMonitorInfo(MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST), &mi)) area = mi.rcWork;
    POINT dst = { area.left + (RectW(area) - w) / 2, area.bottom - h - kBottomGap };
    POINT srcPt = { 0, 0 };
    SIZE  size = { w, h };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)alpha, AC_SRC_ALPHA };
    UpdateLayeredWindow(g_osd, screen, &dst, &size, mem, &srcPt, 0, &bf, ULW_ALPHA);
    SetWindowPos(g_osd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

void StartFade() {
    if (g_fading) return;
    g_fading = true;
    SetTimer(g_osd, TIMER_FADE, 16, NULL);   // ~0.25s 渐隐
}

void StopFade() {
    if (!g_fading) return;
    KillTimer(g_osd, TIMER_FADE);
    g_fading = false;
}

LRESULT CALLBACK OsdWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER:
        if (wParam == TIMER_FADE) {
            g_alpha -= 16;
            if (g_alpha <= 0) {
                StopFade();
                g_alpha = 0;
                ShowWindow(hwnd, SW_HIDE);
            } else {
                BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)g_alpha, AC_SRC_ALPHA };
                UpdateLayeredWindow(hwnd, NULL, NULL, NULL, NULL, NULL, 0, &bf, ULW_ALPHA);
            }
        }
        return 0;
    case WM_DESTROY:
        if (g_osdFont) { DeleteObject(g_osdFont); g_osdFont = NULL; }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

}  // namespace

void KeyOsd_Create() {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = OsdWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"VimouseKeyOsd";
    RegisterClassExW(&wc);
    g_osd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                            L"VimouseKeyOsd", NULL, WS_POPUP, 0, 0, kMinW, kHeight, NULL, NULL, GetModuleHandle(NULL), NULL);
}

void KeyOsd_KeyDown(WORD vk, Modifiers m) {
    if (!g_osd || !g_settings.keyOsd) return;
    if (!IsModifier(vk) && std::find(g_held.begin(), g_held.end(), vk) == g_held.end()) g_held.push_back(vk);
    std::wstring text = BuildText(m);
    if (text.empty()) return;
    // 键盘自动重复：内容没变且正在显示，不必重绘
    if (text == g_text && !g_fading && IsWindowVisible(g_osd) && g_alpha == kAlphaMax) return;
    g_text = text;

    StopFade();
    g_alpha = kAlphaMax;
    Render(g_alpha);
    ShowWindow(g_osd, SW_SHOWNA);
}

void KeyOsd_KeyUp(WORD vk) {
    if (!g_osd) return;
    g_held.erase(std::remove(g_held.begin(), g_held.end(), vk), g_held.end());
    if (!IsWindowVisible(g_osd) || g_fading) return;

    if (!g_held.empty()) {
        // 还有主键按着：刷新文字，不消失
        g_text = BuildText(LiveMods());
        Render(g_alpha);
        return;
    }
    StartFade();
}

void KeyOsd_HideNow() {
    if (!g_osd) return;
    g_held.clear();
    StopFade();
    g_alpha = 0;
    ShowWindow(g_osd, SW_HIDE);
}
