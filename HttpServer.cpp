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

// JSON 转义
static std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// 剪贴板操作
static std::string GetClipboardText() {
    if (!OpenClipboard(NULL)) return "";
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) { CloseClipboard(); return ""; }
    wchar_t* pszText = (wchar_t*)GlobalLock(hData);
    if (!pszText) { CloseClipboard(); return ""; }
    int len = WideCharToMultiByte(CP_UTF8, 0, pszText, -1, NULL, 0, NULL, NULL);
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, pszText, -1, &result[0], len, NULL, NULL);
    GlobalUnlock(hData);
    CloseClipboard();
    return result;
}

static bool SetClipboardText(const std::string& utf8Text) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Text.c_str(), -1, NULL, 0);
    if (wlen <= 0) return false;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(wchar_t));
    if (!hMem) return false;
    wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
    MultiByteToWideChar(CP_UTF8, 0, utf8Text.c_str(), -1, pMem, wlen);
    GlobalUnlock(hMem);
    if (!OpenClipboard(NULL)) { GlobalFree(hMem); return false; }
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();
    return true;
}

// 模拟按键组合
static void SimulateKeyCombo(WORD vk1, WORD vk2) {
    INPUT inputs[4] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk1;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk2;
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = vk2;
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = vk1;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, inputs, sizeof(INPUT));
}

// 屏幕坐标转换辅助
static void GetScreenCoords(double rx, double ry, int& x, int& y) {
    POINT cursorPos;
    GetCursorPos(&cursorPos);
    HMONITOR hMon = MonitorFromPoint(cursorPos, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);
    int screenW = mi.rcMonitor.right - mi.rcMonitor.left;
    int screenH = mi.rcMonitor.bottom - mi.rcMonitor.top;
    x = mi.rcMonitor.left + (int)(rx * screenW);
    y = mi.rcMonitor.top + (int)(ry * screenH);
}

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
    "#bubble{position:fixed;display:none;background:rgba(0,0,0,0.8);color:#0f0;font:14px monospace;"
    "padding:4px 8px;border:1px solid #0f0;border-radius:4px;pointer-events:none;z-index:99;"
    "white-space:pre;max-width:400px;word-break:break-all}"
    "</style></head><body>"
    "<div id='bubble'></div>"
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
    "var g=1,dbl=0,au=0,atm=0,mw=960,gry=0,rt=2000,_ktm=0,_bx=0,_by=0,_bt='';"
    "function _BB(ch){var b=document.getElementById('bubble');"
    "if(ch==='\\b'){_bt=_bt.slice(0,-1)}else if(ch==='\\n'){_bt+='\\n'}else{_bt+=ch}"
    "if(!_bt){b.style.display='none';return}"
    "b.textContent=_bt;b.style.display='block';b.style.left=_bx+'px';b.style.top=_by+'px'}"
    "function _BC(){_bt='';document.getElementById('bubble').style.display='none'}"
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
    "document.querySelectorAll('input[type=range]').forEach(function(el){el.addEventListener('change',function(){this.blur()})});"
    "function TK(){gry=!gry;document.getElementById('bk').textContent=gry?'Gray: ON':'Gray: OFF';"
    "document.getElementById('bk').className=gry?'act':'';SC()}"
    "function R(){_BC();var s=document.getElementById('s');"
    "s.src=(g?'/grid.jpg':'/screenshot.jpg')+'?t='+Date.now()+'&w='+mw+'&gray='+(gry?1:0)}"
    "function SC(){fetch('/api/config?w='+mw+'&grid='+(g?1:0)+'&gray='+(gry?1:0)+'&auto='+(au?1:0)+'&rate='+rt)}"
    "function S(m){document.getElementById('status').textContent=m;"
    "setTimeout(function(){document.getElementById('status').textContent=''},1500)}"
    /* drag state */
    "var _dr=0,_dx1=0,_dy1=0;"
    /* mousedown: record start */
    "document.getElementById('s').addEventListener('mousedown',function(e){"
    "if(e.button!==0)return;e.preventDefault();if(document.activeElement)document.activeElement.blur();"
    "var rect=this.getBoundingClientRect();"
    "_dx1=(e.clientX-rect.left)/rect.width;_dy1=(e.clientY-rect.top)/rect.height;_dr=1"
    "});"
    /* mouseup: click or drag */
    "document.getElementById('s').addEventListener('mouseup',function(e){"
    "if(e.button!==0||!_dr)return;_dr=0;"
    "var rect=this.getBoundingClientRect();"
    "var dx2=(e.clientX-rect.left)/rect.width,dy2=(e.clientY-rect.top)/rect.height;"
    "var dist=Math.sqrt((_dx1-dx2)*(_dx1-dx2)+(_dy1-dy2)*(_dy1-dy2));"
    "if(dist<0.01){"
    "var act=dbl?'dclick':'click';"
    "_bx=e.clientX;_by=e.clientY+16;"
    "S(act+' at '+Math.round(dx2*100)+'%,'+Math.round(dy2*100)+'%');"
    "fetch('/api/click?rx='+dx2+'&ry='+dy2+'&action='+act).then(function(){setTimeout(R,300)})"
    "}else{"
    "S('drag '+Math.round(_dx1*100)+'%,'+Math.round(_dy1*100)+'%->'+Math.round(dx2*100)+'%,'+Math.round(dy2*100)+'%');"
    "fetch('/api/drag?rx1='+_dx1+'&ry1='+_dy1+'&rx2='+dx2+'&ry2='+dy2).then(function(){setTimeout(R,500)})"
    "}"
    "});"
    /* right click */
    "document.getElementById('s').addEventListener('contextmenu',function(e){"
    "e.preventDefault();"
    "var rect=this.getBoundingClientRect();"
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
    /* keyboard input forwarding */
    "var _KS={'Enter':'enter','Backspace':'backspace','Tab':'tab','Escape':'escape',"
    "'ArrowUp':'up','ArrowDown':'down','ArrowLeft':'left','ArrowRight':'right',"
    "'Delete':'delete','Home':'home','End':'end','PageUp':'pageup','PageDown':'pagedown'};"
    "document.addEventListener('keydown',function(e){"
    "if(e.key==='F5'){e.preventDefault();R()}"
    "if(e.ctrlKey&&e.key==='c'){"
    "e.preventDefault();"
    "var x1=new XMLHttpRequest();"
    "x1.open('GET','/api/keypress?key=ctrl_c',false);x1.send();"
    "var x2=new XMLHttpRequest();"
    "x2.open('GET','/api/clipboard',false);x2.send();"
    "var d;try{d=JSON.parse(x2.responseText)}catch(ex){S('Parse failed');return}"
    "if(!d.text){S('Empty clipboard');return}"
    "var ta=document.createElement('textarea');ta.value=d.text;"
    "ta.setAttribute('style','position:fixed;top:0;left:0;width:100px;height:100px;opacity:0.01;user-select:text;-webkit-user-select:text');"
    "document.body.appendChild(ta);ta.focus();ta.select();"
    "var ok=document.execCommand('copy');document.body.removeChild(ta);"
    "S(ok?'Copied: '+d.text.substring(0,40)+(d.text.length>40?'...':''):'execCommand failed')"
    "}"
    "if(e.ctrlKey&&e.key==='v'){"
    "e.preventDefault();"
    "navigator.clipboard.readText().then(function(t){"
    "fetch('/api/clipboard',{method:'POST',body:t}).then(function(){"
    "return fetch('/api/keypress?key=ctrl_v')"
    "}).then(function(){"
    "S('Pasted: '+t.substring(0,40)+(t.length>40?'...':''));"
    "setTimeout(R,300)"
    "})})"
    "}"
    "if(e.ctrlKey&&e.key==='a'){e.preventDefault();fetch('/api/keypress?key=ctrl_a');return}"
    "if(e.ctrlKey&&e.key==='z'){e.preventDefault();fetch('/api/keypress?key=ctrl_z');return}"
    "if(e.ctrlKey&&e.key==='x'){e.preventDefault();"
    "var x1=new XMLHttpRequest();x1.open('GET','/api/keypress?key=ctrl_x',false);x1.send();"
    "var x2=new XMLHttpRequest();x2.open('GET','/api/clipboard',false);x2.send();"
    "var d;try{d=JSON.parse(x2.responseText)}catch(ex){return}"
    "if(d.text){var ta=document.createElement('textarea');ta.value=d.text;"
    "ta.setAttribute('style','position:fixed;top:0;left:0;width:100px;height:100px;opacity:0.01;user-select:text');"
    "document.body.appendChild(ta);ta.focus();ta.select();"
    "document.execCommand('copy');document.body.removeChild(ta);"
    "S('Cut: '+d.text.substring(0,40))}return}"
    "if(e.ctrlKey||e.altKey||e.metaKey)return;"
    "if(e.target.tagName==='INPUT')return;"
    "if(_KS[e.key]){e.preventDefault();fetch('/api/type?special='+_KS[e.key]);"
    "if(e.key==='Backspace')_BB('\\b');else if(e.key==='Enter')_BB('\\n');else S('key: '+e.key);"
    "if(_ktm)clearTimeout(_ktm);_ktm=setTimeout(function(){_ktm=0;R()},rt);return}"
    "if(e.key.length===1){e.preventDefault();fetch('/api/type?char='+encodeURIComponent(e.key));"
    "_BB(e.key);if(_ktm)clearTimeout(_ktm);_ktm=setTimeout(function(){_ktm=0;R()},rt)}"
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
        char reqBuf[65536] = {};
        int totalRecv = 0;
        // 循环读取直到拿到完整请求
        while (totalRecv < (int)sizeof(reqBuf) - 1) {
            int n = recv(client, reqBuf + totalRecv, sizeof(reqBuf) - 1 - totalRecv, 0);
            if (n <= 0) break;
            totalRecv += n;
            // 检查是否读完（GET 请求或 POST body 已读完）
            std::string partial(reqBuf, totalRecv);
            size_t headerEnd = partial.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                // 查找 Content-Length
                size_t clPos = partial.find("Content-Length: ");
                if (clPos == std::string::npos) break; // GET 请求，无 body
                int contentLen = atoi(partial.c_str() + clPos + 16);
                int bodyReceived = totalRecv - (int)headerEnd - 4;
                if (bodyReceived >= contentLen) break; // POST body 读完
            }
        }
        reqBuf[totalRecv] = '\0';

        std::string request(reqBuf, totalRecv);
        std::string path = "/";
        std::string method = "GET";
        std::string postBody;

        // 解析请求方法和路径
        if (request.substr(0, 4) == "GET ") {
            size_t end = request.find(' ', 4);
            if (end != std::string::npos) {
                path = request.substr(4, end - 4);
            }
        } else if (request.substr(0, 5) == "POST ") {
            method = "POST";
            size_t end = request.find(' ', 5);
            if (end != std::string::npos) {
                path = request.substr(5, end - 5);
            }
            // 提取 POST body
            size_t bodyStart = request.find("\r\n\r\n");
            if (bodyStart != std::string::npos) {
                postBody = request.substr(bodyStart + 4);
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
            double rx = atof(getParam("rx").c_str());
            double ry = atof(getParam("ry").c_str());
            std::string action = getParam("action");
            if (action.empty()) action = "click";

            int x, y;
            GetScreenCoords(rx, ry, x, y);
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
        else if (path == "/api/clipboard") {
            if (method == "POST") {
                // 写入远程剪贴板
                bool ok = SetClipboardText(postBody);
                char resp[64];
                snprintf(resp, sizeof(resp), "{\"ok\":%s}", ok ? "true" : "false");
                SendResponse(client, 200, "application/json", resp, (int)strlen(resp));
            } else {
                // 读取远程剪贴板
                std::string text = GetClipboardText();
                std::string escaped = JsonEscape(text);
                std::string resp = "{\"text\":\"" + escaped + "\"}";
                SendResponse(client, 200, "application/json", resp.c_str(), (int)resp.size());
            }
        }
        else if (path == "/api/keypress") {
            std::string key = getParam("key");
            if (key == "ctrl_v") {
                SimulateKeyCombo(VK_CONTROL, 'V');
                Sleep(50);
            } else if (key == "ctrl_c") {
                SimulateKeyCombo(VK_CONTROL, 'C');
                Sleep(200);
            } else if (key == "ctrl_a") {
                SimulateKeyCombo(VK_CONTROL, 'A');
                Sleep(50);
            } else if (key == "ctrl_z") {
                SimulateKeyCombo(VK_CONTROL, 'Z');
                Sleep(50);
            } else if (key == "ctrl_x") {
                SimulateKeyCombo(VK_CONTROL, 'X');
                Sleep(200);
            }
            SendResponse(client, 200, "application/json", "{\"ok\":true}", 11);
        }
        else if (path == "/api/type") {
            std::string ch = getParam("char");
            std::string special = getParam("special");

            if (!ch.empty()) {
                // URL decode: %xx
                std::string decoded;
                for (size_t i = 0; i < ch.size(); i++) {
                    if (ch[i] == '%' && i + 2 < ch.size()) {
                        int val = 0;
                        sscanf_s(ch.c_str() + i + 1, "%2x", &val);
                        decoded += (char)val;
                        i += 2;
                    } else if (ch[i] == '+') {
                        decoded += ' ';
                    } else {
                        decoded += ch[i];
                    }
                }
                // UTF-8 to UTF-16 for SendInput
                int wlen = MultiByteToWideChar(CP_UTF8, 0, decoded.c_str(), -1, NULL, 0);
                if (wlen > 1) {
                    std::vector<wchar_t> wstr(wlen);
                    MultiByteToWideChar(CP_UTF8, 0, decoded.c_str(), -1, wstr.data(), wlen);
                    for (int i = 0; i < wlen - 1; i++) {
                        INPUT inputs[2] = {};
                        inputs[0].type = INPUT_KEYBOARD;
                        inputs[0].ki.wScan = wstr[i];
                        inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
                        inputs[1].type = INPUT_KEYBOARD;
                        inputs[1].ki.wScan = wstr[i];
                        inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
                        SendInput(2, inputs, sizeof(INPUT));
                    }
                }
            } else if (!special.empty()) {
                WORD vk = 0;
                if (special == "enter") vk = VK_RETURN;
                else if (special == "backspace") vk = VK_BACK;
                else if (special == "tab") vk = VK_TAB;
                else if (special == "escape") vk = VK_ESCAPE;
                else if (special == "up") vk = VK_UP;
                else if (special == "down") vk = VK_DOWN;
                else if (special == "left") vk = VK_LEFT;
                else if (special == "right") vk = VK_RIGHT;
                else if (special == "delete") vk = VK_DELETE;
                else if (special == "home") vk = VK_HOME;
                else if (special == "end") vk = VK_END;
                else if (special == "pageup") vk = VK_PRIOR;
                else if (special == "pagedown") vk = VK_NEXT;

                if (vk) {
                    INPUT inputs[2] = {};
                    inputs[0].type = INPUT_KEYBOARD;
                    inputs[0].ki.wVk = vk;
                    inputs[1].type = INPUT_KEYBOARD;
                    inputs[1].ki.wVk = vk;
                    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(2, inputs, sizeof(INPUT));
                }
            }
            SendResponse(client, 200, "application/json", "{\"ok\":true}", 11);
        }
        else if (path == "/api/drag") {
            double rx1 = atof(getParam("rx1").c_str());
            double ry1 = atof(getParam("ry1").c_str());
            double rx2 = atof(getParam("rx2").c_str());
            double ry2 = atof(getParam("ry2").c_str());

            int x1, y1, x2, y2;
            GetScreenCoords(rx1, ry1, x1, y1);
            GetScreenCoords(rx2, ry2, x2, y2);

            // 移到起点，按下，滑到终点，松开
            SetCursorPos(x1, y1);
            Sleep(30);
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            Sleep(30);
            // 分步移动，模拟真实拖拽
            int steps = 10;
            for (int i = 1; i <= steps; i++) {
                int cx = x1 + (x2 - x1) * i / steps;
                int cy = y1 + (y2 - y1) * i / steps;
                SetCursorPos(cx, cy);
                Sleep(10);
            }
            Sleep(30);
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);

            char resp[128];
            snprintf(resp, sizeof(resp), "{\"ok\":true,\"from\":[%d,%d],\"to\":[%d,%d]}", x1, y1, x2, y2);
            SendResponse(client, 200, "application/json", resp, (int)strlen(resp));
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
