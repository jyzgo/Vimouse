#include "Overlay.h"

HWND CreateOverlayWindow(const wchar_t* className, WNDPROC proc, DWORD exStyle,
                         int x, int y, int w, int h, BYTE alpha) {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = className;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);  // 已注册时返回失败，忽略

    HWND hwnd = CreateWindowExW(exStyle | WS_EX_LAYERED, className, NULL, WS_POPUP,
                                x, y, w, h, NULL, NULL, GetModuleHandle(NULL), NULL);
    if (hwnd) {
        SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
        ShowWindow(hwnd, SW_HIDE);
    }
    return hwnd;
}

DoubleBuffer::DoubleBuffer(HWND h) : hwnd(h) {
    hdc = BeginPaint(hwnd, &ps);
    GetClientRect(hwnd, &rc);
    mem = CreateCompatibleDC(hdc);
    bmp = CreateCompatibleBitmap(hdc, Width(), Height());
    oldBmp = SelectObject(mem, bmp);
    SetBkMode(mem, TRANSPARENT);
}

DoubleBuffer::~DoubleBuffer() {
    BitBlt(hdc, 0, 0, Width(), Height(), mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

HFONT MakeFont(int height, int weight, const wchar_t* face, DWORD pitchFamily) {
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, pitchFamily, face);
}
