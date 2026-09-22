// Util.h - 零依赖小工具
#pragma once
#include <string>

bool IsSystemChinese();
std::wstring Utf8ToWide(const std::string& s);
std::string  WideToUtf8(const std::wstring& w);
