// Hint.h - 26x26 字母坐标叠加层（两个字母跳到格子中心；第一个字母 = 行，第二个 = 列）
#pragma once
#include <windows.h>

void CreateHintWindow();
void EnterHintMode();
void ExitHintMode(bool showMiniGrid);   // true: 退出后在光标处进入一层微调 grid
void HintTypeLetter(char letter);       // 'A'..'Z'
bool HintClearTyped();                  // 清掉已输的第一个字母；没有可清的返回 false

RECT  HintCellRect(const RECT& area, int col, int row);
POINT HintCellCenter(const RECT& area, int col, int row);
