// winsock2 必须在 windows.h 之前
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>

#include "HttpServer.h"
#include "ScreenOCR.h"
#include <thread>
#include <fstream>
#include <vector>
#include <string>

#pragma comment(lib, "ws2_32.lib")

static std::thread* g_httpThread = nullptr;
static bool g_httpRunning = false;
static SOCKET g_listenSocket = INVALID_SOCKET;

// 读取文件到 vector
static bool ReadFileBytes(const std::string& path, std::vector<char>& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    auto size = file.tellg();
    file.seekg(0);
    out.resize((size_t)size);
    file.read(out.data(), size);
    return true;
}

// 发送 HTTP 响应
static void SendResponse(SOCKET client, int statusCode, const char* contentType,
                          const char* body, int bodyLen) {
    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 %d OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-cache\r\n"
        "\r\n",
        statusCode, contentType, bodyLen);
    send(client, header, (int)strlen(header), 0);
    if (body && bodyLen > 0) {
        send(client, body, bodyLen, 0);
    }
}

static const char* HTML_PAGE =
    "<!DOCTYPE html><html><head><title>Vimouse</title>"
    "<style>"
    "body{margin:0;background:#111;display:flex;flex-direction:column;align-items:center;height:100vh}"
    "img{max-width:100vw;max-height:calc(100vh - 40px);object-fit:contain}"
    ".bar{height:36px;display:flex;align-items:center;gap:12px;color:#aaa;font:14px monospace;padding:2px 10px}"
    "button{background:#333;color:#0f0;border:1px solid #555;padding:4px 14px;cursor:pointer;font:14px monospace}"
    "button:hover{background:#444}.active{background:#060;border-color:#0f0}"
    "</style></head><body>"
    "<div class='bar'>"
    "<button id='bg' class='active' onclick='T(1)'>Grid</button>"
    "<button id='bc' onclick='T(0)'>Clean</button>"
    "</div>"
    "<img id='s' src='/grid.jpg'/>"
    "<script>"
    "var g=1;"
    "function T(v){g=v;document.getElementById('bg').className=g?'active':'';"
    "document.getElementById('bc').className=g?'':'active';R()}"
    "function R(){document.getElementById('s').src=(g?'/grid.jpg':'/screenshot.jpg')+'?t='+Date.now()}"
    "setInterval(R,2000);"
    "</" "script>"
    "</body></html>";

static void HttpServerThread(int port) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    g_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listenSocket == INVALID_SOCKET) {
        DebugLog("HttpServer: socket() failed\n");
        return;
    }

    // 允许端口复用
    int opt = 1;
    setsockopt(g_listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons((u_short)port);

    if (bind(g_listenSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        DebugLog("HttpServer: bind() failed on port %d\n", port);
        closesocket(g_listenSocket);
        g_listenSocket = INVALID_SOCKET;
        return;
    }

    listen(g_listenSocket, 5);
    DebugLog("HttpServer: listening on 127.0.0.1:%d\n", port);

    while (g_httpRunning) {
        // 用 select 设超时，避免永久阻塞
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(g_listenSocket, &readSet);
        timeval tv = { 1, 0 }; // 1秒超时

        int sel = select(0, &readSet, NULL, NULL, &tv);
        if (sel <= 0) continue;

        SOCKET client = accept(g_listenSocket, NULL, NULL);
        if (client == INVALID_SOCKET) continue;

        // 读取请求
        char reqBuf[2048] = {};
        recv(client, reqBuf, sizeof(reqBuf) - 1, 0);

        std::string request(reqBuf);
        std::string path = "/";

        // 解析 GET 路径
        if (request.substr(0, 4) == "GET ") {
            size_t end = request.find(' ', 4);
            if (end != std::string::npos) {
                path = request.substr(4, end - 4);
            }
        }

        // 去掉查询字符串
        size_t qpos = path.find('?');
        if (qpos != std::string::npos) path = path.substr(0, qpos);

        if (path == "/screenshot.jpg" || path == "/grid.jpg") {
            bool grid = (path == "/grid.jpg");
            std::string result = CaptureCurrentScreen(60, grid);

            if (result.substr(0, 3) == "OK ") {
                std::string filePath = result.substr(3);
                std::vector<char> fileData;
                if (ReadFileBytes(filePath, fileData)) {
                    SendResponse(client, 200, "image/jpeg", fileData.data(), (int)fileData.size());
                } else {
                    SendResponse(client, 500, "text/plain", "read failed", 11);
                }
            } else {
                SendResponse(client, 500, "text/plain", result.c_str(), (int)result.size());
            }
        } else {
            // 返回 HTML 页面
            SendResponse(client, 200, "text/html; charset=utf-8",
                         HTML_PAGE, (int)strlen(HTML_PAGE));
        }

        closesocket(client);
    }

    closesocket(g_listenSocket);
    g_listenSocket = INVALID_SOCKET;
    WSACleanup();
}

void StartHttpServer(int port) {
    if (g_httpThread) return;
    g_httpRunning = true;
    g_httpThread = new std::thread(HttpServerThread, port);
}

void StopHttpServer() {
    g_httpRunning = false;
    if (g_listenSocket != INVALID_SOCKET) {
        closesocket(g_listenSocket);
    }
    if (g_httpThread && g_httpThread->joinable()) {
        g_httpThread->join();
        delete g_httpThread;
        g_httpThread = nullptr;
    }
}
