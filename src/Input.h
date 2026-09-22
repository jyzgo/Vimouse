// Input.h - 鼠标动作（本地 mouse_event / 远程模式转发为管道命令）
#pragma once

enum class Button { Left, Right, Middle };

void MouseDown(Button b);
void MouseUp(Button b);
void Click(Button b);            // 按下-短暂停留-抬起
void Scroll(unsigned moveBit);   // MV_UP / MV_DOWN / MV_LEFT / MV_RIGHT
void ToggleDrag();               // 左键按住 / 释放（g_isDragging）
