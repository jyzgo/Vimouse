// PointPeek.h - 长按 a/s 时在之前的输入点上画带编号的小圆（0 = 松开会跳到的点）
#pragma once
#include <windows.h>

constexpr int PEEK_MAX = 5;

void PointPeek_Create();
void PointPeek_Destroy();
void PointPeek_Show(const POINT* pts, int n);   // n 最多 PEEK_MAX，多的忽略
void PointPeek_Hide();
