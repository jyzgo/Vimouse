#pragma once
#include "Common.h"

// 获取当前活动窗口信息 (JSON)
std::string GetActiveWindowInfo();

// 列出所有可见窗口 (JSON array)
std::string ListWindows();

// 按标题关键字查找窗口 (JSON)
std::string FindWindow(const std::string& titleContains);

// 等待窗口出现，超时返回错误
std::string WaitForWindow(const std::string& titleContains, int timeoutMs);
