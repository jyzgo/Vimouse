// Mover.h - 按住方向键时的平滑加速移动线程
#pragma once

void StartSmoothMove();   // 有方向键按下时调用（幂等）
void StopSmoothMove();    // 所有方向键松开时调用（幂等，会 join）
