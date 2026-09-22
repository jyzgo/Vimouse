// Screens.h - 显示器枚举与几何小工具
#pragma once
#include <windows.h>

void  RefreshScreens();            // 重新枚举到 g_screenRects（至少含一个屏）
int   GetCurrentScreenIndex();     // 光标所在屏，找不到返回 0
RECT  ScreenRectAt(int index);     // 越界回退到 0
POINT RectCenter(const RECT& r);
int   RectW(const RECT& r);
int   RectH(const RECT& r);
