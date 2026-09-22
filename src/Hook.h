// Hook.h - 低级键盘钩子：把按键翻译成动作
#pragma once
#include <windows.h>

bool InstallKeyboardHook();
void UninstallKeyboardHook();

void SetActive(bool active);        // 激活/停用（含光标、标签、指示器等副作用）
void ToggleActive(bool centerCursor);
void ToggleRemote();
