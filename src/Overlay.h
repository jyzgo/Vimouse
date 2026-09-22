// Overlay.h - 叠加层窗口的公共小工具
#pragma once
#include <windows.h>

// 注册（幂等）并创建一个 WS_POPUP 分层窗口，初始隐藏
HWND CreateOverlayWindow(const wchar_t* className, WNDPROC proc, DWORD exStyle,
                         int x, int y, int w, int h, BYTE alpha);

// 双缓冲绘制辅助：BeginPaint 后拿到 memDC，析构时 BitBlt 回去
struct DoubleBuffer {
    HWND hwnd; PAINTSTRUCT ps; HDC hdc; HDC mem; HBITMAP bmp; HGDIOBJ oldBmp; RECT rc;
    explicit DoubleBuffer(HWND h);
    ~DoubleBuffer();
    int Width() const { return rc.right - rc.left; }
    int Height() const { return rc.bottom - rc.top; }
};

HFONT MakeFont(int height, int weight, const wchar_t* face, DWORD pitchFamily = DEFAULT_PITCH | FF_SWISS);
