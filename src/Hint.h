// Hint.h - 26x26 字母坐标叠加层（两个字母跳到格子中心）
#pragma once
#include <windows.h>

void CreateHintWindow();
void EnterHintMode();
void ExitHintMode(bool showMiniGrid);   // true: 退出后在光标处进入一层微调 grid
void HintTypeLetter(char letter);       // 'A'..'Z'

RECT  HintCellRect(const RECT& area, int col, int row);
POINT HintCellCenter(const RECT& area, int col, int row);
