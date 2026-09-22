// CommandHandler.h - 管道文本命令 → 动作。运行在 Pipe 线程。
#pragma once
#include <string>

// 命令格式如 "move 500 300" / "click" / "pos"，返回 "OK ..." 或 "ERR ..."
std::string HandleCommand(const std::string& command);
