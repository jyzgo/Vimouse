// KeyOsd.h - 屏幕底部居中的按键提示：按下即显示，松开后渐隐
#pragma once
#include <windows.h>
#include "Keymap.h"

void KeyOsd_Create();
void KeyOsd_KeyDown(WORD vk, Modifiers m);
void KeyOsd_KeyUp(WORD vk);
void KeyOsd_HideNow();
// 在按键提示的位置显示一行状态（如「Vimouse 已开启」），停留约 1 秒后渐隐；松开按键不会提前收起
void KeyOsd_ShowStatus(const std::wstring& text);
// 光标处的一次性脉冲：圆环收拢到十字准星上，约 0.45 秒；on=true 绿色，false 灰色
void KeyOsd_CursorPulse(bool on, unsigned int ms = 450);
