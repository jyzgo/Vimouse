#include "Screens.h"
#include "State.h"

static BOOL CALLBACK MonitorProc(HMONITOR hMon, HDC, LPRECT, LPARAM) {
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfo(hMon, &mi)) g_screenRects.push_back(mi.rcMonitor);
    return TRUE;
}

void RefreshScreens() {
    g_screenRects.clear();
    EnumDisplayMonitors(NULL, NULL, MonitorProc, 0);
    if (g_screenRects.empty()) {
        RECT r = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
        g_screenRects.push_back(r);
    }
}

int GetCurrentScreenIndex() {
    POINT p;
    GetCursorPos(&p);
    for (size_t i = 0; i < g_screenRects.size(); i++)
        if (PtInRect(&g_screenRects[i], p)) return (int)i;
    return 0;
}

RECT ScreenRectAt(int index) {
    if (g_screenRects.empty()) RefreshScreens();
    if (index < 0 || index >= (int)g_screenRects.size()) index = 0;
    return g_screenRects[index];
}

POINT RectCenter(const RECT& r) { return { r.left + (r.right - r.left) / 2, r.top + (r.bottom - r.top) / 2 }; }
int RectW(const RECT& r) { return r.right - r.left; }
int RectH(const RECT& r) { return r.bottom - r.top; }
