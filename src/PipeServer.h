// PipeServer.h - 命名管道 \\.\pipe\vimouse 服务端 + CLI 客户端
#pragma once
#include "Common.h"

void StartPipeServer();
void StopPipeServer();

// CLI：发送一条命令并打印结果。返回 0=成功 1=错误
int RunCLIClient(const std::string& command);
// CLI：逐行执行脚本文件（# 开头为注释）
int RunCLIScript(const std::string& filePath);
// stdin→pipe 桥接（远程控制端通过 ssh 调用：Vimouse.exe --pipe-stdin）
int RunPipeStdinBridge();
