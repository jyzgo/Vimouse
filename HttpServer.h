#pragma once
#include "Common.h"

// 启动 HTTP 截图服务器 (localhost:port)
// 访问 / 返回最新截图的 HTML 页面（自动刷新）
// 访问 /screenshot.jpg 返回最新截图 JPEG
void StartHttpServer(int port = 59123);
void StopHttpServer();
