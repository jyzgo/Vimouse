#pragma once
#include "Common.h"

// 启动 Named Pipe 服务端线程
void StartPipeServer();

// 停止 Named Pipe 服务端线程
void StopPipeServer();

// CLI 客户端：连接 Pipe 发送命令，返回结果
// 返回值：0=成功，1=错误
int RunCLIClient(const std::string& command);

// CLI 客户端：执行脚本文件
int RunCLIScript(const std::string& filePath);

// stdin→pipe 桥接模式（用于远程控制）
int RunPipeStdinBridge();
