// Clickables.h - 「可点击元素」模式：用 UI Automation 扫描当前屏幕上的按钮 / 链接 / 图标 / 列表项等，
// 在屏幕上标出来；hjkl / 方向键跳到该方向最近的目标，输入标签字母直接跳，f/g 点击。
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "Keymap.h"

void CreateClickWindow();
void DestroyClickWindow();
void EnterClickMode();               // 扫描光标所在屏幕
void ExitClickMode();
bool HandleClickKeyDown(DWORD vk, Modifiers m);   // true = 吞掉按键
int  ClickModeStatus();              // -1 = 扫描中，否则 = 目标个数

// 同步扫描（任意线程可调，内部自行 CoInitialize）。report 非空时写入各窗口统计
std::vector<RECT> ScanClickables(const RECT& area, std::string* report);
