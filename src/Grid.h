// Grid.h - 二分定位叠加层（半区 / 象限逐级缩小）
#pragma once

void CreateGridWindow();
void EnterGridMode();            // 以当前屏幕为区域，光标移到屏幕中心
void EnterGridModeAtCursor();    // 以当前屏幕为区域，首次分割以光标为中心
void EnterMiniGrid();            // Hint 之后：光标周围一个 hint 格大小的单层微调
void ExitGridMode();
void GridSelect(unsigned moveBit);   // MV_LEFT.. 半区，MV_UPLEFT.. 象限
void GridBack();                     // 返回上一级；已是顶层则退出
