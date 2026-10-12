// Util.h - 零依赖小工具
#pragma once
#include <string>

// 界面是否用中文：设置里「语言」= 跟随系统(默认) 时看系统 UI 语言，否则按用户选择
bool IsSystemChinese();
// 0 = 跟随系统，1 = 中文，2 = English（LoadSettings / 保存设置时调用）
void SetUiLanguage(int lang);
std::wstring Utf8ToWide(const std::string& s);
std::string  WideToUtf8(const std::wstring& w);
