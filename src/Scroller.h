// Scroller.h - 滚轮模式的平滑滚动：按下立即滚、按住逐渐加速、松开后短暂滑行停下
// 代替旧的"每次按键/自动重复发一格 300"——那种方式按住时先卡 ~0.5s（系统重复延迟），之后又一跳一跳
#pragma once

void ScrollKeyDown(unsigned moveBit);   // MV_UP / MV_DOWN / MV_LEFT / MV_RIGHT；自动重复按键会被忽略
void ScrollKeyUp(unsigned bits);        // 松开（可以一次传多个方向位）
void ScrollStopAll();                   // 退出滚轮模式 / 停用：清掉按住状态，已有速度照常滑行停下
