// HelpOverlay.h - 可拖动的半透明快捷键速查小窗（位置持久化）
#pragma once
#include <windows.h>

void CreateHelpWindow();
void ToggleHelpWindow();
void ShowHelpDialog(HWND owner);   // 弹出 MessageBox 版操作指南
