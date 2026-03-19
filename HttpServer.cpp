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
#include <shlobj.h>
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

// 配置持久化
struct HttpConfig {
    int width = 960;
    bool grid = true;
    bool grayscale = false;
    bool autoRefresh = false;
    int rate = 2000; // auto refresh interval in ms (200-5000)
};
static HttpConfig g_config;

static std::string GetConfigPath() {
    char path[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path);
    std::string dir = std::string(path) + "\\Vimouse";
    CreateDirectoryA(dir.c_str(), NULL);
    return dir + "\\http_config.txt";
}

static void LoadConfig() {
    std::ifstream f(GetConfigPath());
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.substr(0, 6) == "width=") g_config.width = atoi(line.c_str() + 6);
        else if (line == "grid=1") g_config.grid = true;
        else if (line == "grid=0") g_config.grid = false;
        else if (line == "gray=1") g_config.grayscale = true;
        else if (line == "gray=0") g_config.grayscale = false;
        else if (line == "auto=1") g_config.autoRefresh = true;
        else if (line == "auto=0") g_config.autoRefresh = false;
        else if (line.substr(0, 5) == "rate=") g_config.rate = atoi(line.c_str() + 5);
    }
}

static void SaveConfig() {
    std::ofstream f(GetConfigPath());
    f << "width=" << g_config.width << "\n";
    f << "grid=" << (g_config.grid ? 1 : 0) << "\n";
    f << "gray=" << (g_config.grayscale ? 1 : 0) << "\n";
    f << "auto=" << (g_config.autoRefresh ? 1 : 0) << "\n";
    f << "rate=" << g_config.rate << "\n";
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
    "<!DOCTYPE html><html><head><title>Vimouse Remote</title>"
    "<style>"
    "body{margin:0;background:#111;display:flex;flex-direction:column;align-items:center;height:100vh;overflow:hidden;user-select:none}"
    "img{width:100vw;height:calc(100vh - 40px);object-fit:fill;cursor:crosshair;image-rendering:auto}"
    ".bar{height:36px;display:flex;align-items:center;gap:10px;color:#aaa;font:13px monospace;padding:2px 10px;width:100%;box-sizing:border-box}"
    "button{background:#333;color:#0f0;border:1px solid #555;padding:3px 12px;cursor:pointer;font:13px monospace;border-radius:3px}"
    "button:hover{background:#444}.act{background:#060;border-color:#0f0}"
    "#status{color:#ff0;margin-left:auto}"
    "</style></head><body>"
    "<div class='bar'>"
    "<button id='bg' class='act' onclick='TG(1)'>Grid</button>"
    "<button id='bc' onclick='TG(0)'>Clean</button>"
    "<button onclick='R()'>Refresh (F5)</button>"
    "<button onclick='DC()'>DblClick Mode</button>"
    "<button id='ba' onclick='TA()'>Auto: OFF</button>"
    "<button id='bk' onclick='TK()'>Gray: OFF</button>"
    "<span style='color:#888'>Hz:</span><input id='rr' type='range' min='200' max='5000' value='2000' step='100' style='width:80px' oninput='RR()'>"
    "<span id='rl' style='color:#0f0'>2.0s</span>"
    "<span style='color:#888'>W:</span><input id='wr' type='range' min='320' max='1920' value='960' step='160' style='width:100px' oninput='WR()'>"
    "<span id='wl' style='color:#0f0'>960</span>"
    "<span id='status'></span>"
    "</div>"
    "<img id='s' src='/grid.jpg' draggable='false'/>"
    "<script>"
    "var g=1,dbl=0,au=0,atm=0,mw=960,gry=0,rt=2000;"
    "function TG(v){g=v;document.getElementById('bg').className=g?'act':'';"
    "document.getElementById('bc').className=g?'':'act';SC();R()}"
    "function DC(){dbl=!dbl;document.getElementById('status').textContent=dbl?'DblClick ON':''}"
    "function TA(){au=!au;document.getElementById('ba').textContent=au?'Auto: ON':'Auto: OFF';"
    "document.getElementById('ba').className=au?'act':'';"
    "if(atm)clearInterval(atm);atm=au?setInterval(R,rt):0;SC()}"
    "function RR(){rt=parseInt(document.getElementById('rr').value);"
    "document.getElementById('rl').textContent=(rt/1000).toFixed(1)+'s';"
    "if(au&&atm){clearInterval(atm);atm=setInterval(R,rt)}SC()}"
    "function WR(){mw=document.getElementById('wr').value;document.getElementById('wl').textContent=mw;SC()}"
    "function TK(){gry=!gry;document.getElementById('bk').textContent=gry?'Gray: ON':'Gray: OFF';"
    "document.getElementById('bk').className=gry?'act':'';SC()}"
    "function R(){var s=document.getElementById('s');"
    "s.src=(g?'/grid.jpg':'/screenshot.jpg')+'?t='+Date.now()+'&w='+mw+'&gray='+(gry?1:0)}"
    "function SC(){fetch('/api/config?w='+mw+'&grid='+(g?1:0)+'&gray='+(gry?1:0)+'&auto='+(au?1:0)+'&rate='+rt)}"
    "function S(m){document.getElementById('status').textContent=m;"
    "setTimeout(function(){document.getElementById('status').textContent=''},1500)}"
    /* click handler */
    "document.getElementById('s').addEventListener('click',function(e){"
    "var img=this,rect=img.getBoundingClientRect();"
    "var rx=(e.clientX-rect.left)/rect.width,ry=(e.clientY-rect.top)/rect.height;"
    "if(rx<0||rx>1||ry<0||ry>1)return;"
    "var act=dbl?'dclick':'click';"
    "S(act+' at '+Math.round(rx*100)+'%,'+Math.round(ry*100)+'%');"
    "fetch('/api/click?rx='+rx+'&ry='+ry+'&action='+act).then(function(){setTimeout(R,300)})"
    "});"
    /* right click */
    "document.getElementById('s').addEventListener('contextmenu',function(e){"
    "e.preventDefault();"
    "var img=this,rect=img.getBoundingClientRect();"
    "var rx=(e.clientX-rect.left)/rect.width,ry=(e.clientY-rect.top)/rect.height;"
    "if(rx<0||rx>1||ry<0||ry>1)return;"
    "S('rclick at '+Math.round(rx*100)+'%,'+Math.round(ry*100)+'%');"
    "fetch('/api/click?rx='+rx+'&ry='+ry+'&action=rclick').then(function(){setTimeout(R,300)})"
    "});"
    /* scroll handler */
    "document.getElementById('s').addEventListener('wheel',function(e){"
    "e.preventDefault();"
    "var dir=e.deltaY>0?'down':'up',amt=Math.min(Math.abs(Math.round(e.deltaY/120)),10)||1;"
    "fetch('/api/scroll?dir='+dir+'&amt='+amt);"
    "setTimeout(R,400)"
    "},{passive:false});"
    /* F5 key */
    "document.addEventListener('keydown',function(e){"
    "if(e.key==='F5'){e.preventDefault();R()}"
    "});"
    /* 加载保存的配置 */
    "fetch('/api/config').then(function(r){return r.json()}).then(function(c){"
    "if(c.width){mw=c.width;document.getElementById('wr').value=mw;document.getElementById('wl').textContent=mw}"
    "if(c.grid!==undefined){g=c.grid?1:0;document.getElementById('bg').className=g?'act':'';document.getElementById('bc').className=g?'':'act'}"
    "if(c.gray){gry=1;document.getElementById('bk').textContent='Gray: ON';document.getElementById('bk').className='act'}"
    "if(c.rate){rt=c.rate;document.getElementById('rr').value=rt;document.getElementById('rl').textContent=(rt/1000).toFixed(1)+'s'}"
    "if(c.auto){au=1;document.getElementById('ba').textContent='Auto: ON';document.getElementById('ba').className='act';atm=setInterval(R,rt)}"
    "R()}).catch(function(){R()});"
    "</" "script>"
    "</body></html>";

static void HttpServerThread(int port) {
    LoadConfig();

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

        // 分离路径和查询字符串
        std::string queryStr;
        size_t qpos = path.find('?');
        if (qpos != std::string::npos) {
            queryStr = path.substr(qpos + 1);
            path = path.substr(0, qpos);
        }

        // 辅助：从查询字符串解析参数
        auto getParam = [&](const std::string& key) -> std::string {
            std::string search = key + "=";
            size_t pos = queryStr.find(search);
            if (pos == std::string::npos) return "";
            size_t start = pos + search.size();
            size_t end = queryStr.find('&', start);
            return (end == std::string::npos) ? queryStr.substr(start) : queryStr.substr(start, end - start);
        };

        if (path == "/screenshot.jpg" || path == "/grid.jpg") {
            bool grid = (path == "/grid.jpg");
            int maxW = 1920;
            std::string wParam = getParam("w");
            if (!wParam.empty()) maxW = atoi(wParam.c_str());
            if (maxW < 160) maxW = 160;
            if (maxW > 3840) maxW = 3840;
            bool gray = getParam("gray") == "1";
            // 质量随分辨率调整
            int q = maxW <= 480 ? 40 : (maxW <= 960 ? 55 : 65);
            std::string result = CaptureCurrentScreen(q, grid, maxW, gray);
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
        }
        else if (path == "/api/config") {
            // 有参数则保存配置
            std::string wParam = getParam("w");
            if (!wParam.empty()) {
                g_config.width = atoi(wParam.c_str());
                g_config.grid = getParam("grid") != "0";
                g_config.grayscale = getParam("gray") == "1";
                g_config.autoRefresh = getParam("auto") == "1";
                std::string rateParam = getParam("rate");
                if (!rateParam.empty()) {
                    int r = atoi(rateParam.c_str());
                    if (r >= 200 && r <= 5000) g_config.rate = r;
                }
                SaveConfig();
            }
            // 返回当前配置
            char json[256];
            snprintf(json, sizeof(json),
                "{\"width\":%d,\"grid\":%s,\"gray\":%s,\"auto\":%s,\"rate\":%d}",
                g_config.width,
                g_config.grid ? "true" : "false",
                g_config.grayscale ? "true" : "false",
                g_config.autoRefresh ? "true" : "false",
                g_config.rate);
            SendResponse(client, 200, "application/json", json, (int)strlen(json));
        }
        else if (path == "/api/click") {
            // 参数: rx, ry (0.0-1.0 比例), action (click/rclick/dclick)
            double rx = atof(getParam("rx").c_str());
            double ry = atof(getParam("ry").c_str());
            std::string action = getParam("action");
            if (action.empty()) action = "click";

            // 获取鼠标所在屏幕
            POINT cursorPos;
            GetCursorPos(&cursorPos);
            HMONITOR hMon = MonitorFromPoint(cursorPos, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfo(hMon, &mi);

            int screenW = mi.rcMonitor.right - mi.rcMonitor.left;
            int screenH = mi.rcMonitor.bottom - mi.rcMonitor.top;
            int x = mi.rcMonitor.left + (int)(rx * screenW);
            int y = mi.rcMonitor.top + (int)(ry * screenH);

            SetCursorPos(x, y);
            Sleep(15);

            if (action == "rclick") {
                mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
                Sleep(30);
                mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
            } else if (action == "dclick") {
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                Sleep(30);
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                Sleep(50);
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                Sleep(30);
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            } else {
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                Sleep(30);
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            }

            char resp[128];
            snprintf(resp, sizeof(resp), "{\"ok\":true,\"x\":%d,\"y\":%d}", x, y);
            SendResponse(client, 200, "application/json", resp, (int)strlen(resp));
        }
        else if (path == "/api/scroll") {
            std::string dir = getParam("dir");
            int amt = atoi(getParam("amt").c_str());
            if (amt < 1) amt = 1;
            int delta = amt * 120;

            if (dir == "up") {
                mouse_event(MOUSEEVENTF_WHEEL, 0, 0, delta, 0);
            } else if (dir == "down") {
                mouse_event(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)(-delta), 0);
            }

            SendResponse(client, 200, "application/json", "{\"ok\":true}", 11);
        }
        else {
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
