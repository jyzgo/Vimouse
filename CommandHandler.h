#pragma once
#include "Common.h"

// 解析并执行一条命令，返回结果字符串
// 命令格式: "move 500 300", "click 500 300", "pos", etc.
std::string HandleCommand(const std::string& command);
