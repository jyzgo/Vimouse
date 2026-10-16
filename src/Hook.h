// Hook.h - 低级键盘钩子：把按键翻译成动作
#pragma once
#include <windows.h>

bool InstallKeyboardHook();
void UninstallKeyboardHook();

void SetActive(bool active);        // 激活/停用（含光标、标签、指示器等副作用）
void ToggleActive(bool centerCursor);
void ToggleRemote();
void SetArrowMode(bool on);         // 方向键模式开关（开启时会退出鼠标模式）
// 设置 exit_on_unbound_key 开启时：退出 Vimouse + 关闭提示 + 光标脉冲，返回 true（调用方应放行该键）；
// 设置关闭时什么都不做，返回 false
bool ExitOnUnboundKey();
