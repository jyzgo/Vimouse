// KeyOsd.h - 屏幕底部居中的按键提示：按下即显示，松开后渐隐
#pragma once
#include <windows.h>
#include "Keymap.h"

void KeyOsd_Create();
void KeyOsd_KeyDown(WORD vk, Modifiers m);
void KeyOsd_KeyUp(WORD vk);
void KeyOsd_HideNow();
