// History.h - 点击位置历史（r/e 前后跳）
#pragma once
#include <windows.h>

void AddMousePositionToStack();
void GoToPreviousPosition();
void GoToNextPosition();

// 输入点历史（最多 100 个）：每次 f / Enter / 点击并放标签 都记录（AddMousePositionToStack 内）。
// JumpToInputPoint(true, n)：往更早方向跳 n 步（1 = 上一个）；false：往更近方向。翻过头回到起点。
// 光标被移开（不在上次跳到的点）就算结束回溯，下次从最新点重新开始。
void JumpToInputPoint(bool older, int steps = 1);
// 预览：往某方向接下来会到达的点（out[0] = 跳 1 步的点），返回个数
int PeekInputPoints(bool older, POINT* out, int maxN);
