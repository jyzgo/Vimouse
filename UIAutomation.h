#pragma once
#include "Common.h"

// 初始化 UI Automation (在程序启动时调用一次)
bool InitUIAutomation();

// 清理 UI Automation
void CleanupUIAutomation();

// 查找 UI 元素 by name (可选 type 过滤)，返回 JSON
std::string FindUIElement(const std::string& name, const std::string& type = "");

// 列出窗口的子 UI 元素，返回 JSON array
std::string ListUIElements(HWND hwnd, int maxDepth = 2);

// 查找并点击 UI 元素
std::string ClickUIElement(const std::string& name, const std::string& type = "");
