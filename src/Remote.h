// Remote.h - 远程控制：ssh <host> "Vimouse.exe --pipe-stdin"，本机按键转为管道命令发过去
#pragma once
#include <string>

void SendRemoteCmd(const std::string& cmd);
bool StartRemoteMode(const std::string& host, const std::string& remoteExePath);
void StopRemoteMode();
