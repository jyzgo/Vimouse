#include "Common.h"
#include "PipeServer.h"
#include "Resource.h"
#include <shlobj.h>
#include <shellapi.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>

// 全局变量
HHOOK g_keyboardHook = NULL;
bool g_isActive = true;  // 默认激活状态
int g_mouseSpeed = 10;  // 基础移动速度
int g_wheelSpeed = 300;
bool g_isDragging = false;  // 是否正在拖动
POINT g_lastMousePos;  // 记录鼠标拖动开始位置
int g_currentScreenIndex = 0;  // 当前屏幕索引
std::vector<RECT> g_screenRects;  // 存储所有屏幕的矩形区域
bool g_firstCPress = true;  // 标记是否是第一次按C键
bool g_hintMode = false;  // 是否处于hint模式
bool g_wheelMode = false;  // 是否处于滚轮模式
bool g_gridMode = false;  // 是否处于grid模式
std::vector<RECT> g_gridStack;  // 存储grid模式下的区域栈
HWND g_hintWindow = NULL;  // 单个窗口绘制所有hint
std::string g_currentHint = "";  // 当前输入的hint字符
int g_hintScreenIndex = 0;  // hint模式所在的屏幕索引
bool g_lastActionWasC = false;  // 记录上一个操作是否是C键
HWND g_indicatorWindow = NULL;  // 状态指示器窗口
bool g_leftButtonDown = false;  // 左键按下状态
bool g_rightButtonDown = false;  // 右键按下状态
HWND g_gridWindow = NULL;  // Grid模式窗口
HWND g_hwnd = NULL;  // 主窗口句柄
bool g_exitRequested = false;  // 退出标志
int g_dotSize = 1;

bool g_iskeyDown = false;

// Ctrl键状态跟踪
bool g_ctrlPressed = false;  // 跟踪Ctrl键状态
bool g_altPressed = false;
bool g_winPressed = false;   // 跟踪Win键状态
bool g_shiftPressed = false; // 跟踪Shift键状态
bool g_clickFlash = false;   // 点击闪烁标记
bool g_miniGridMode = false; // hint后的单层mini grid模式
bool g_gridCustomCenter = false;  // grid模式是否使用自定义中心点
POINT g_gridCenter = { 0, 0 };     // grid自定义中心点（屏幕坐标）
HWND g_helpWindow = NULL;    // 帮助悬浮窗
bool g_helpVisible = false;  // 帮助窗口是否可见

// 平滑移动相关变量
bool g_hPressed = false;
bool g_jPressed = false;
bool g_kPressed = false;
bool g_lPressed = false;
bool g_uPressed = false;
bool g_oPressed = false;
bool g_nPressed = false;
bool g_dotPressed = false;  // 修改：改为dotPressed表示句号键
std::thread* g_moveThread = nullptr;
bool g_shouldMove = false;

// 加速相关变量
int g_originalSpeed = 10;  // 记录原始速度
bool g_isAccelerating = false;  // 是否正在加速
int g_acceleratedSpeed = 10;  // 每次加速的增量
int g_maxSpeed = 2000;  // 最大速度限制
int g_lastSetSpeed = 10;  // 记录最后一次设置的速度值

// 位置容器相关变量
std::vector<POINT> g_positionStack;  // 存储鼠标位置的容器
int g_mousePosIndex = -1;  // 当前位置索引，-1表示没有记录
const int MAX_POSITIONS = 10;  // 容器最大容量




// 标签系统相关变量
std::vector<TagInfo> g_tags;
char g_nextLetter = 'A';  // 下一个要使用的字母
bool g_tagMode = false;  // 是否处于tag模式
bool g_editTagMode = false;
int g_tagTabIndex = 1;

// 自定义光标
static HCURSOR g_cursorIdle = NULL;     // Vimouse 待机光标（绿色十字准星）
static HCURSOR g_cursorMoving = NULL;   // 按下移动键时的光标（橙色实心圆点）

static HCURSOR CreateCrosshairCursor(BYTE r, BYTE g, BYTE b, BYTE centerR, BYTE centerG, BYTE centerB) {
    const int size = 32;
    const int hot = 15;

    HDC screenDC = GetDC(NULL);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    DWORD* pixels = nullptr;
    HBITMAP hColor = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, (void**)&pixels, NULL, 0);
    if (!hColor || !pixels) { ReleaseDC(NULL, screenDC); return NULL; }

    memset(pixels, 0, size * size * 4);

    auto setPixel = [&](int x, int y, BYTE pr, BYTE pg, BYTE pb, BYTE pa) {
        if (x >= 0 && x < size && y >= 0 && y < size)
            pixels[y * size + x] = (pa << 24) | (pr << 16) | (pg << 8) | pb;
    };

    int cx = hot, cy = hot;

    // 十字线 (中间留空)
    for (int dx = -10; dx <= 10; dx++) {
        if (dx >= -2 && dx <= 2) continue;
        setPixel(cx + dx, cy, r, g, b, 255);
        setPixel(cx + dx, cy - 1, 0, 0, 0, 140);
        setPixel(cx + dx, cy + 1, 0, 0, 0, 140);
    }
    for (int dy = -10; dy <= 10; dy++) {
        if (dy >= -2 && dy <= 2) continue;
        setPixel(cx, cy + dy, r, g, b, 255);
        setPixel(cx - 1, cy + dy, 0, 0, 0, 140);
        setPixel(cx + 1, cy + dy, 0, 0, 0, 140);
    }

    // 中心点（3x3 实心）
    for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
            setPixel(cx + dx, cy + dy, centerR, centerG, centerB, 255);

    // Mask
    HBITMAP hMask = CreateBitmap(size, size, 1, 1, NULL);
    HDC maskDC = CreateCompatibleDC(screenDC);
    SelectObject(maskDC, hMask);
    PatBlt(maskDC, 0, 0, size, size, WHITENESS);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
            if ((pixels[y * size + x] >> 24) > 0)
                SetPixel(maskDC, x, y, RGB(0, 0, 0));
    DeleteDC(maskDC);

    ICONINFO ii = {};
    ii.fIcon = FALSE;
    ii.xHotspot = hot;
    ii.yHotspot = hot;
    ii.hbmMask = hMask;
    ii.hbmColor = hColor;
    HCURSOR hCur = (HCURSOR)CreateIconIndirect(&ii);

    DeleteObject(hColor);
    DeleteObject(hMask);
    ReleaseDC(NULL, screenDC);
    return hCur;
}

static void InitCustomCursors() {
    if (!g_cursorIdle)
        g_cursorIdle = CreateCrosshairCursor(0, 220, 120, 255, 60, 60);     // 绿色准星 + 红中心
    if (!g_cursorMoving)
        g_cursorMoving = CreateCrosshairCursor(255, 160, 0, 255, 255, 0);   // 橙色准星 + 黄中心
}

#ifndef OCR_NORMAL
#define OCR_NORMAL 32512
#endif

static void ApplyCursor(HCURSOR cursor) {
    if (cursor) {
        HCURSOR copy = CopyCursor(cursor);
        SetSystemCursor(copy, OCR_NORMAL);
    }
}

static void SetVimouseCursor() {
    InitCustomCursors();
    ApplyCursor(g_cursorIdle);
}

static void SetMovingCursor() {
    InitCustomCursors();
    ApplyCursor(g_cursorMoving);
}

static void RestoreSystemCursor() {
    SystemParametersInfo(SPI_SETCURSORS, 0, NULL, 0);
}

// 函数声明
void UpdateNextLetter();
void StartSmoothMove();
void StopSmoothMove();
void EnterHintMode();
void ExitHintMode();
void ExitWheelMode();
void EnterGridModeFromCurrentPos();
void EnterGridMode();
void ExitGridMode();
void MoveToGridArea(int direction);
void MoveToGridCorner(int corner);
void ReturnToPreviousGrid();
bool RemoveTagBySameLetter(char letter);
void PutTag(POINT currentPos);
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK GridWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK HintWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK IndicatorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK TagWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void UpdateIndicatorPosition();
void CreateGridWindow();
void CreateHintWindow();
void CreateIndicatorWindow();
void CreateTagWindow(int x, int y, char letter);
HWND CreateTagWindowNoSave(int x, int y, char letter);
void RemoveTagWindow(int x, int y);
void EnterTagMode();
void ExitTagMode();
bool JumpToTag(char letter);
BOOL CALLBACK EnumDisplayMonitorsProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData);
int GetCurrentScreenIndex();
void AddMousePositionToStack();
void GoToPreviousPosition();
void GoToNextPosition();
long DistanceSquared(POINT a, POINT b);
std::string GetConfigPath();
void SaveTagsToConfig();
void LoadTagsFromConfig();
void RemoveTagWindowByLetter(char letter);

void HideAllTagWindows() {
    for (auto& tag : g_tags) {
        if (tag.hwnd && IsWindow(tag.hwnd)) {
            ShowWindow(tag.hwnd, SW_HIDE);
        }
    }
}

void ShowTagWindowsNonInteractive() {
    for (auto& tag : g_tags) {
        if (!tag.hwnd || !IsWindow(tag.hwnd)) continue;

        // 设置窗口为“透明”（鼠标穿透）+ 不激活
        LONG_PTR exStyle = GetWindowLongPtr(tag.hwnd, GWL_EXSTYLE);
        exStyle |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
        SetWindowLongPtr(tag.hwnd, GWL_EXSTYLE, exStyle);

        // 显示但不激活
        ShowWindow(tag.hwnd, SW_SHOWNA);

        // 刷新窗口样式（重要）
        SetWindowPos(tag.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

void ShowTagWindowsInteractive() {
    for (auto& tag : g_tags) {
        if (!tag.hwnd || !IsWindow(tag.hwnd)) continue;

        // 移除鼠标穿透，但仍禁止激活（可点击但不抢焦点）
        LONG_PTR exStyle = GetWindowLongPtr(tag.hwnd, GWL_EXSTYLE);
        exStyle &= ~WS_EX_TRANSPARENT;   // 允许接收鼠标事件
        exStyle |= WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;     // 仍不抢焦点（可选，根据需求）
        // 如果你希望点击时能激活窗口，则也移除 WS_EX_NOACTIVATE：
        // exStyle &= ~WS_EX_NOACTIVATE;
        SetWindowLongPtr(tag.hwnd, GWL_EXSTYLE, exStyle);

        ShowWindow(tag.hwnd, SW_SHOWNA);

        SetWindowPos(tag.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

// 枚举显示器回调函数
BOOL CALLBACK EnumDisplayMonitorsProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfo(hMonitor, &mi)) {
        g_screenRects.push_back(mi.rcMonitor);  // 使用工作区域（不包括任务栏）
    }
    return TRUE;
}

void DebugLog(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    OutputDebugStringA(buffer);
}

static bool IsSystemChinese();  // 前向声明
// ============ 帮助悬浮窗 ============

static std::string GetHelpConfigPath() {
    char path[MAX_PATH];
    HRESULT result = SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, path);
    if (FAILED(result)) return "";
    return std::string(path) + "/Vimouse/.help_pos";
}

static void SaveHelpPos() {
    if (!g_helpWindow) return;
    RECT r;
    GetWindowRect(g_helpWindow, &r);
    std::string path = GetHelpConfigPath();
    if (path.empty()) return;
    std::ofstream f(path);
    if (f.is_open()) {
        f << r.left << " " << r.top << " " << (g_helpVisible ? 1 : 0) << "\n";
        f.close();
    }
}

static void LoadHelpPos(int& x, int& y, bool& visible) {
    x = -1; y = -1; visible = false;
    std::string path = GetHelpConfigPath();
    if (path.empty()) return;
    std::ifstream f(path);
    if (f.is_open()) {
        int v = 0;
        f >> x >> y >> v;
        visible = (v != 0);
        f.close();
    }
}

static HFONT g_helpFont = NULL;

LRESULT CALLBACK HelpWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        // 背景
        HBRUSH bg = CreateSolidBrush(RGB(20, 20, 30));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        if (!g_helpFont)
            g_helpFont = CreateFont(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

        SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ oldFont = SelectObject(hdc, g_helpFont);

        const char** lines;
        int lineCount;

        static const char* lines_zh[] = {
            " Vimouse \xBF\xEC\xCB\xD9\xB2\xCE\xBF\xBC",  // placeholder, use wide below
            "", "", "", "", "", "", "", "", "",
            "", "", "", "", "", "", "", "", "",
        };
        // Use wide char for Chinese
        const wchar_t* wlines_zh[] = {
            L" Vimouse \u5FEB\u901F\u53C2\u8003",
            L" \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500",
            L" Ctrl+J  \u5F00\u5173\u6FC0\u6D3B",
            L" h/j/k/l \u79FB\u52A8(\u957F\u6309\u52A0\u901F)",
            L" Shift+  \u7CBE\u786E1\u50CF\u7D20",
            L" u/o/n/. \u5BF9\u89D2\u79FB\u52A8",
            L" f  \u5DE6\u952E(\u6309\u4F4F)",
            L" g  \u53F3\u952E",
            L" b  \u4E2D\u952E",
            L" v  \u62D6\u62FD\u5F00\u5173",
            L" m  Hint\u8DF3\u8F6C(2\u5B57\u6BCD)",
            L" i  Grid\u4E8C\u5206\u5B9A\u4F4D",
            L" y  \u6EDA\u8F6E\u6A21\u5F0F",
            L" w  \u6807\u7B7E\u8DF3\u8F6C",
            L" q  \u653E\u7F6E\u6807\u7B7E",
            L" c  \u5C4F\u5E55\u4E2D\u5FC3/\u5207\u6362",
            L" r/e  \u524D\u8FDB/\u540E\u9000\u4F4D\u7F6E",
            L" Enter  \u70B9\u51FB+\u9000\u51FA",
            L" Esc    \u9000\u51FA\u6A21\u5F0F",
        };
        const char* lines_en[] = {
            " Vimouse Quick Ref",
            " -----------------",
            " Ctrl+J  Toggle ON/OFF",
            " h/j/k/l Move (hold=accel)",
            " Shift+  Precise 1px",
            " u/o/n/. Diagonal",
            " f  Left click (hold)",
            " g  Right click",
            " b  Middle click",
            " v  Drag toggle",
            " m  Hint jump (2-letter)",
            " i  Grid bisect mode",
            " y  Scroll mode",
            " w  Tag jump mode",
            " q  Place tag",
            " c  Screen center/switch",
            " r/e  Prev/Next position",
            " Enter  Click + exit",
            " Esc    Exit mode",
        };
        bool isChinese = IsSystemChinese();
        lineCount = 19;

        for (int i = 0; i < lineCount; i++) {
            RECT tr = { 4, 4 + i * 15, rc.right - 4, 4 + (i + 1) * 15 };
            if (i == 0) {
                SetTextColor(hdc, RGB(80, 255, 160));
            } else if (i == 1) {
                SetTextColor(hdc, RGB(60, 60, 80));
            } else {
                SetTextColor(hdc, RGB(180, 180, 200));
            }
            if (isChinese) {
                DrawTextW(hdc, wlines_zh[i], -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            } else {
                DrawTextA(hdc, lines_en[i], -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
        }

        SelectObject(hdc, oldFont);
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_LBUTTONDOWN:
        // 允许拖动窗口
        SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        break;
    case WM_MOVE:
        // 位置变化时保存
        SaveHelpPos();
        break;
    case WM_CLOSE:
        g_helpVisible = false;
        ShowWindow(hwnd, SW_HIDE);
        SaveHelpPos();
        return 0; // 不销毁窗口
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void CreateHelpWindow() {
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = HelpWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"VimouseHelpClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassEx(&wc);

    int helpW = 210, helpH = 300;

    // 加载上次位置
    int posX, posY;
    bool wasVisible;
    LoadHelpPos(posX, posY, wasVisible);

    if (posX < 0 || posY < 0) {
        // 默认：右下角
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        posX = screenW - helpW - 20;
        posY = screenH - helpH - 60;
    }

    g_helpWindow = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        L"VimouseHelpClass",
        L"Vimouse Help",
        WS_POPUP,
        posX, posY, helpW, helpH,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );

    if (g_helpWindow) {
        SetLayeredWindowAttributes(g_helpWindow, 0, 180, LWA_ALPHA);
        g_helpVisible = wasVisible;
        ShowWindow(g_helpWindow, wasVisible ? SW_SHOWNA : SW_HIDE);
    }
}

void ToggleHelpWindow() {
    if (!g_helpWindow) return;
    g_helpVisible = !g_helpVisible;
    if (g_helpVisible) {
        SetWindowPos(g_helpWindow, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        ShowWindow(g_helpWindow, SW_HIDE);
    }
    SaveHelpPos();
}

// 获取配置文件路径
std::string GetConfigPath() {
    char path[MAX_PATH];
    HRESULT result = SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, path);
    if (FAILED(result)) {
        // 如果获取用户配置文件夹失败，使用当前目录
        GetModuleFileNameA(NULL, path, MAX_PATH);
        char* lastSlash = strrchr(path, '\\');
        if (lastSlash) {
            *(lastSlash + 1) = '\0';
        }
        strcat_s(path, MAX_PATH, "Vimouse.config");
        return std::string(path);
    }

    std::string configPath = std::string(path) + "/Vimouse/.config";
    return configPath;
}

// 保存标签到配置文件
void SaveTagsToConfig() {
    std::string configPath = GetConfigPath();

    // 确保目录存在
    size_t lastSlash = configPath.find_last_of("/");
    if (lastSlash != std::string::npos) {
        std::string dirPath = configPath.substr(0, lastSlash);
        CreateDirectoryA(dirPath.c_str(), NULL);
    }

    std::ofstream file(configPath);
    if (file.is_open()) {
        for (const auto& tag : g_tags) {
            if (tag.active) {
                // 格式: key,letter,x,y
                long long key = (long long)tag.pos.x * 10000 + tag.pos.y;
                file << key << "," << tag.letter << "," << tag.pos.x << "," << tag.pos.y << "\n";
            }
        }
        file.close();
    }
}

// 从配置文件加载标签
void LoadTagsFromConfig() {
    std::string configPath = GetConfigPath();
    std::ifstream file(configPath);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            std::istringstream iss(line);
            std::string keyStr, letterStr, xStr, yStr;
            if (std::getline(iss, keyStr, ',') &&
                std::getline(iss, letterStr, ',') &&
                std::getline(iss, xStr, ',') &&
                std::getline(iss, yStr)) {

                long long key = std::stoll(keyStr);
                char letter = letterStr[0];
                int x = std::stoi(xStr);
                int y = std::stoi(yStr);

                RemoveTagBySameLetter(letter);
                // 创建标签窗口并添加到全局标签向量
                HWND hwnd = CreateTagWindowNoSave(x, y, letter);

                // 手动添加到标签列表
                TagInfo tag;
                tag.hwnd = hwnd;
                tag.pos.x = x;
                tag.pos.y = y;
                tag.letter = letter;
                tag.active = true;
                g_tags.push_back(tag);

                // 更新下一个字母
                if (letter >= g_nextLetter) {
                    g_nextLetter = (char)(letter + 1);
                    if (g_nextLetter > 'Z') {
                        g_nextLetter = 'A'; // 如果超过Z则回到A
                    }
                }
            }
        }
        file.close();
    }
}


// 创建标签窗口的辅助函数（不保存配置）
HWND CreateTagWindowNoSave(int x, int y, char letter) {
    // 注册tag窗口类
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = TagWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"TagWindowClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassEx(&wc);

    // 创建tag窗口
    HWND hwnd = CreateWindowEx(
		WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,  // 置顶且支持透明
        L"TagWindowClass",
        NULL,
        WS_POPUP,
        x-15, y-15, 30, 30,  // 位置和大小
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (hwnd) {
        // 设置窗口透明度 (半透)
        SetLayeredWindowAttributes(hwnd, 0, g_tagMode ? 255 : 150, LWA_ALPHA);
        ShowWindow(hwnd, SW_SHOW);
    }

    return hwnd;
}






// 获取鼠标当前所在屏幕的索引
int GetCurrentScreenIndex() {
    POINT cursorPos;
    GetCursorPos(&cursorPos);

    for (size_t i = 0; i < g_screenRects.size(); i++) {
        if (PtInRect(&g_screenRects[i], cursorPos)) {
            return i;
        }
    }
    //DebugLog("find screen id %d\n");
    // 如果找不到鼠标所在屏幕（理论上不应该发生），返回第一个屏幕
    return 0;
}

// 将当前鼠标位置添加到容器中
void AddMousePositionToStack() {
    POINT currentPos;
    GetCursorPos(&currentPos);

    //get last position
    if (!g_positionStack.empty()) {
        POINT lastPos = g_positionStack.back();
        // 如果当前位置与上一个位置相同，则不添加
        if (currentPos.x == lastPos.x && currentPos.y == lastPos.y) {
            return;
        }
        if (DistanceSquared(currentPos, lastPos) < 25) { // 距离小于5像素
            return;
        }
    }

    // 如果容器已满，移除最早的位置
    if (g_positionStack.size() >= MAX_POSITIONS) {
        g_positionStack.erase(g_positionStack.begin());
        // 调整索引
        if (g_mousePosIndex >= 0) {
            g_mousePosIndex--;
        }
    }

    // 添加当前位置
    g_positionStack.push_back(currentPos);
    // 设置索引为最后一个位置
    g_mousePosIndex = g_positionStack.size() - 1;
}

void UpdatePosByindex() {
    // 1. 首先检查堆栈是否为空
    if (g_positionStack.empty()) {
        // 堆栈为空，无法设置位置，处理错误（例如记录日志或直接返回）
        OutputDebugString(L"Error: Position stack is empty.\n");
        return;
    }

    // 2. 检查索引是否在合法范围内 [0, g_positionStack.size() - 1]
    if (g_mousePosIndex < 0 || g_mousePosIndex >= g_positionStack.size()) {
        // 索引越界，处理错误
        OutputDebugString(L"Error: Mouse position index out of range.\n");
        return;
    }

    // 3. 安全地获取目标位置并设置光标
    // 使用 at() 函数可以进行运行时边界检查，更安全[9](@ref)。
    // 当然，由于我们已经做了手动检查，使用 [] 也是安全的。
    POINT targetPos = g_positionStack.at(g_mousePosIndex); // 或者使用 g_positionStack[g_mousePosIndex]
    SetCursorPos(targetPos.x, targetPos.y);
}

// 回到上一个位置（从后往前）
void GoToPreviousPosition() {
    if (g_mousePosIndex > 0 && g_positionStack.size() > 0) {
        g_mousePosIndex--;
        UpdatePosByindex();

    }
    else if (g_mousePosIndex == 0)
    {
        g_mousePosIndex = g_positionStack.size() - 1;
        UpdatePosByindex();

    }

}

// 前进到下一个位置
void GoToNextPosition() {
    if (g_mousePosIndex >= 0 && g_mousePosIndex < (int)g_positionStack.size() - 1) {
        g_mousePosIndex++;
        UpdatePosByindex();
    }
    else if (g_mousePosIndex >= (int)g_positionStack.size() - 1)
    {
        g_mousePosIndex = 0;
        UpdatePosByindex();
    }
}

// 平滑移动线程函数
void SmoothMoveThread() {
    DWORD accelerationStartTime = GetTickCount();
    while (g_shouldMove) {
        POINT currentPos;
        GetCursorPos(&currentPos);
        int newX = currentPos.x;
        int newY = currentPos.y;
        bool moved = false;

        // Shift 按下时精确模式：1像素移动，不加速
        if (g_shiftPressed) {
            if (g_hPressed) { newX -= 1; moved = true; }
            if (g_jPressed) { newY += 1; moved = true; }
            if (g_kPressed) { newY -= 1; moved = true; }
            if (g_lPressed) { newX += 1; moved = true; }
            if (g_uPressed) { newX -= 1; newY -= 1; moved = true; }
            if (g_oPressed) { newX += 1; newY -= 1; moved = true; }
            if (g_nPressed) { newX -= 1; newY += 1; moved = true; }
            if (g_dotPressed) { newX += 1; newY += 1; moved = true; }
        } else {
            // 正常模式：动态加速
            if (g_hPressed || g_jPressed || g_kPressed || g_lPressed || g_uPressed || g_oPressed || g_nPressed || g_dotPressed) {
                if (GetTickCount() - accelerationStartTime > 5) {
                    g_mouseSpeed += g_acceleratedSpeed;
                    if (g_mouseSpeed > g_maxSpeed) {
                        g_mouseSpeed = g_maxSpeed;
                    }
                    accelerationStartTime = GetTickCount();
                }
            }

            if (g_hPressed) { newX -= g_mouseSpeed / 10; moved = true; }
            if (g_jPressed) { newY += g_mouseSpeed / 10; moved = true; }
            if (g_kPressed) { newY -= g_mouseSpeed / 10; moved = true; }
            if (g_lPressed) { newX += g_mouseSpeed / 10; moved = true; }
            if (g_uPressed) { newX -= g_mouseSpeed / 7; newY -= g_mouseSpeed / 7; moved = true; }
            if (g_oPressed) { newX += g_mouseSpeed / 7; newY -= g_mouseSpeed / 7; moved = true; }
            if (g_nPressed) { newX -= g_mouseSpeed / 7; newY += g_mouseSpeed / 7; moved = true; }
            if (g_dotPressed) { newX += g_mouseSpeed / 7; newY += g_mouseSpeed / 7; moved = true; }
        }

        if (moved) {
            SetCursorPos(newX, newY);

            // 移动线程不操作任何窗口（避免跨线程 UI 导致卡死）
            // 坐标标签在 StopSmoothMove → UpdateIndicatorPosition 时更新

            // 如果正在拖动，继续发送拖动事件
            if (g_isDragging) {
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            }
        }

        // 控制移动频率，使移动更平滑

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// 开始平滑移动
void StartSmoothMove() {
    if (!g_shouldMove) {
        g_shouldMove = true;
        SetMovingCursor();
        g_moveThread = new std::thread(SmoothMoveThread);
    }
}

// 停止平滑移动
void StopSmoothMove() {
    if (g_shouldMove) {
        g_shouldMove = false;
        if (g_moveThread && g_moveThread->joinable()) {
            g_moveThread->join();
            delete g_moveThread;
            g_moveThread = nullptr;
        }
        // 如果正在加速，恢复到上次设置的速度
        g_mouseSpeed = g_lastSetSpeed;
        g_isAccelerating = false;
        if (g_isActive) SetVimouseCursor(); // 恢复待机光标
        UpdateIndicatorPosition();
    }
}

// 创建Tag窗口的窗口过程
LRESULT CALLBACK TagWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // 获取窗口客户区大小
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        // 创建内存DC进行双缓冲绘制
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc,
            clientRect.right - clientRect.left,
            clientRect.bottom - clientRect.top);
        HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);

        // 绘制黑色背景
        HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(memDC, &clientRect, blackBrush);
        DeleteObject(blackBrush);

        // 设置文本颜色和模式
        SetTextColor(memDC, RGB(255, 255, 255)); // 白色文字
        SetBkMode(memDC, TRANSPARENT);

        // 根据tag模式设置字体大小
        int fontSize = g_tagMode ? 24 : 16;
        HFONT font = CreateFont(
            fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
        HGDIOBJ oldFont = SelectObject(memDC, font);

        // 绘制字母
        char letter[2] = { 'A', '\0' }; // 临时初始化
        for (auto& tag : g_tags) {
            if (tag.hwnd == hwnd) {
                letter[0] = tag.letter;
                break;
            }
        }

        RECT textRect = { 0, 0, clientRect.right, clientRect.bottom };
        DrawTextA(memDC, letter, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // 恢复字体并删除
        SelectObject(memDC, oldFont);
        DeleteObject(font);

        // 将内存DC内容复制到实际DC
        BitBlt(hdc, 0, 0,
            clientRect.right - clientRect.left,
            clientRect.bottom - clientRect.top,
            memDC, 0, 0, SRCCOPY);

        // 清理资源
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        break;
    }
    case WM_LBUTTONDOWN: {
        // 在tag模式下点击标签
        if (g_tagMode) {
            char letter = 'A'; // 临时初始化
            for (const auto& tag : g_tags) {
                if (tag.hwnd == hwnd) {
                    letter = tag.letter;
                    break;
                }
            }
            RemoveTagWindowByLetter(letter);
            
        }
        else {
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
        break;
    }
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// 创建Grid窗口的窗口过程
// Grid 缓存资源（只创建一次）
static HBRUSH g_gridBgBrush = NULL;
static HPEN g_gridCrossPen = NULL;
static HPEN g_gridDiagPen = NULL;
static HFONT g_gridLabelFont = NULL;
static int g_gridLabelFontSize = 0;

static void EnsureGridResources(int fontSize) {
    if (!g_gridBgBrush) g_gridBgBrush = CreateSolidBrush(RGB(0, 0, 0));
    if (!g_gridCrossPen) g_gridCrossPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    if (!g_gridDiagPen) g_gridDiagPen = CreatePen(PS_SOLID, 1, RGB(120, 120, 120));
    if (!g_gridLabelFont || g_gridLabelFontSize != fontSize) {
        if (g_gridLabelFont) DeleteObject(g_gridLabelFont);
        g_gridLabelFont = CreateFont(fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Consolas");
        g_gridLabelFontSize = fontSize;
    }
}

LRESULT CALLBACK GridWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc,
            clientRect.right - clientRect.left,
            clientRect.bottom - clientRect.top);
        HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);

        int w = clientRect.right - clientRect.left;
        int h = clientRect.bottom - clientRect.top;
        int midX, midY;

        if (g_gridCustomCenter && !g_gridStack.empty()) {
            // 将屏幕坐标转为窗口坐标
            RECT sr = g_gridStack.back();
            midX = g_gridCenter.x - sr.left;
            midY = g_gridCenter.y - sr.top;
            // 限制在窗口内
            if (midX < 1) midX = 1;
            if (midX >= w) midX = w - 1;
            if (midY < 1) midY = 1;
            if (midY >= h) midY = h - 1;
        } else {
            midX = w / 2;
            midY = h / 2;
        }

        // 字体大小随窗口缩放
        int fontSize = min(w, h) / 6;
        if (fontSize < 12) fontSize = 12;
        if (fontSize > 28) fontSize = 28;

        EnsureGridResources(fontSize);

        // 背景
        FillRect(memDC, &clientRect, g_gridBgBrush);
        SetBkMode(memDC, TRANSPARENT);

        // 十字线
        HGDIOBJ oldPen = SelectObject(memDC, g_gridCrossPen);
        MoveToEx(memDC, midX, 0, NULL); LineTo(memDC, midX, h);
        MoveToEx(memDC, 0, midY, NULL); LineTo(memDC, w, midY);

        // 对角线（从四角到中心点）
        SelectObject(memDC, g_gridDiagPen);
        MoveToEx(memDC, 0, 0, NULL); LineTo(memDC, midX, midY);
        MoveToEx(memDC, w, 0, NULL); LineTo(memDC, midX, midY);
        MoveToEx(memDC, 0, h, NULL); LineTo(memDC, midX, midY);
        MoveToEx(memDC, w, h, NULL); LineTo(memDC, midX, midY);
        SelectObject(memDC, oldPen);

        // 方向键标签
        HGDIOBJ oldFont = SelectObject(memDC, g_gridLabelFont);

        // 标签偏向中心点（75% 靠近中心）
        int lx = midX * 3 / 4;                      // 左半，靠近中心
        int rx = midX + (w - midX) / 4;              // 右半，靠近中心
        int ty = midY * 3 / 4;                       // 上半，靠近中心
        int by = midY + (h - midY) / 4;              // 下半，靠近中心
        struct { const char* key; int x; int y; COLORREF color; } labels[] = {
            { "H", lx,   midY, RGB(50, 255, 120) },   // 左
            { "L", rx,   midY, RGB(50, 255, 120) },   // 右
            { "K", midX, ty,   RGB(80, 200, 255) },   // 上
            { "J", midX, by,   RGB(80, 200, 255) },   // 下
            { "U", lx,   ty,   RGB(200, 200, 100) },  // 左上
            { "O", rx,   ty,   RGB(200, 200, 100) },  // 右上
            { "N", lx,   by,   RGB(200, 200, 100) },  // 左下
            { ".",  rx,   by,   RGB(200, 200, 100) },  // 右下
        };

        HBRUSH labelBg = CreateSolidBrush(RGB(0, 0, 0));
        for (auto& lb : labels) {
            RECT tr = { lb.x - fontSize / 2 - 2, lb.y - fontSize / 2 - 1,
                        lb.x + fontSize / 2 + 2, lb.y + fontSize / 2 + 1 };
            FillRect(memDC, &tr, labelBg);
            SetTextColor(memDC, lb.color);
            DrawTextA(memDC, lb.key, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        DeleteObject(labelBg);

        SelectObject(memDC, oldFont);

        // 将内存DC内容复制到实际DC
        BitBlt(hdc, 0, 0,
            clientRect.right - clientRect.left,
            clientRect.bottom - clientRect.top,
            memDC, 0, 0, SRCCOPY);

        // 清理资源
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        break;
    }
    case WM_LBUTTONDOWN: {
        // 点击窗口处理
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        // 计算点击的格子
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        int blockWidth = (clientRect.right - clientRect.left) / 26;
        int blockHeight = (clientRect.bottom - clientRect.top) / 26;

        int col = x / blockWidth;
        int row = y / blockHeight;

        if (col >= 0 && col < 26 && row >= 0 && row < 26) {
            // 移动鼠标到对应位置
            if (g_hintScreenIndex >= 0 && g_hintScreenIndex < (int)g_screenRects.size()) {
                const RECT& screenRect = g_screenRects[g_hintScreenIndex];
                int finalX = screenRect.left + col * blockWidth + blockWidth / 2;
                int finalY = screenRect.top + row * blockHeight + blockHeight / 2;

                SetCursorPos(finalX, finalY);
            }

            // 退出hint模式
            g_hintMode = false;
            g_currentHint = "";
            ShowWindow(hwnd, SW_HIDE);
        }
        break;
    }
    case WM_RBUTTONDOWN:
        // 右键退出hint模式
        g_hintMode = false;
        g_currentHint = "";
        ShowWindow(hwnd, SW_HIDE);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// 创建Hint窗口的窗口过程
LRESULT CALLBACK HintWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!g_isActive)
    {
		return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // 获取窗口客户区大小
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        // 创建内存DC进行双缓冲绘制
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc,
            clientRect.right - clientRect.left,
            clientRect.bottom - clientRect.top);
        HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);

        // 绘制黑色背景
        HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(memDC, &clientRect, blackBrush);
        DeleteObject(blackBrush);

        // 计算每个格子的大小
        int blockWidth = (clientRect.right - clientRect.left) / 26;
        int blockHeight = (clientRect.bottom - clientRect.top) / 26;

        // 设置文本颜色和模式
        SetTextColor(memDC, RGB(255, 255, 255));
        SetBkMode(memDC, TRANSPARENT);
        HFONT font = CreateFont(
            blockHeight / 2, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
        HGDIOBJ oldFont = SelectObject(memDC, font);

        // 交替背景色画刷（棋盘格区分边界）
        HBRUSH darkBrush = CreateSolidBrush(RGB(15, 15, 25));
        HBRUSH lightBrush = CreateSolidBrush(RGB(35, 35, 50));

        // 绘制所有hint字符
        for (int row = 0; row < 26; row++) {
            for (int col = 0; col < 26; col++) {
                RECT cellRect = {
                    col * blockWidth,
                    row * blockHeight,
                    (col + 1) * blockWidth,
                    (row + 1) * blockHeight
                };

                // 棋盘格背景
                FillRect(memDC, &cellRect, ((row + col) % 2) ? lightBrush : darkBrush);

                // 如果是hint模式下第一个字母，只显示匹配列
                if (g_currentHint.length() == 1) {
                    if (col != (g_currentHint[0] - 'A')) {
                        continue;
                    }
                }

                char hintText[3];
                hintText[0] = 'A' + col;
                hintText[1] = 'A' + row;
                hintText[2] = '\0';

                DrawTextA(memDC, hintText, -1, &cellRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
        DeleteObject(darkBrush);
        DeleteObject(lightBrush);

        // 恢复字体并删除
        SelectObject(memDC, oldFont);
        DeleteObject(font);

        // 将内存DC内容复制到实际DC
        BitBlt(hdc, 0, 0,
            clientRect.right - clientRect.left,
            clientRect.bottom - clientRect.top,
            memDC, 0, 0, SRCCOPY);

        // 清理资源
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        break;
    }
    case WM_LBUTTONDOWN: {
        if (!g_isActive)
        {
			return DefWindowProc(hwnd, msg, wParam, lParam);
        }
        // 点击窗口处理
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        // 计算点击的格子
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        int blockWidth = (clientRect.right - clientRect.left) / 26;
        int blockHeight = (clientRect.bottom - clientRect.top) / 26;

        int col = x / blockWidth;
        int row = y / blockHeight;

        if (col >= 0 && col < 26 && row >= 0 && row < 26) {
            // 移动鼠标到对应位置
            if (g_gridMode && g_gridStack.size() > 0) {
                // Grid模式下，移动到对应区域
                RECT currentRect = g_gridStack.back();
                int regionWidth = currentRect.right - currentRect.left;
                int regionHeight = currentRect.bottom - currentRect.top;

                int finalX = currentRect.left + col * (regionWidth / 26) + (regionWidth / 26) / 2;
                int finalY = currentRect.top + row * (regionHeight / 26) + (regionHeight / 26) / 2;

                SetCursorPos(finalX, finalY);
            }
            else if (g_hintScreenIndex >= 0 && g_hintScreenIndex < (int)g_screenRects.size()) {
                const RECT& screenRect = g_screenRects[g_hintScreenIndex];
                int finalX = screenRect.left + col * blockWidth + blockWidth / 2;
                int finalY = screenRect.top + row * blockHeight + blockHeight / 2;

                SetCursorPos(finalX, finalY);
            }

            // 退出hint模式
            g_hintMode = false;
            g_currentHint = "";
            ShowWindow(hwnd, SW_HIDE);
        }
        break;
    }
    case WM_RBUTTONDOWN:
        // 右键退出hint模式
        g_hintMode = false;
        g_currentHint = "";
        ShowWindow(hwnd, SW_HIDE);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// 状态指示器窗口过程
// 计算鼠标在 hint 网格中的坐标字母（如 "HK"）
static void GetHintCoord(POINT mousePos, char* out) {
    out[0] = '?'; out[1] = '?'; out[2] = '\0';
    for (size_t i = 0; i < g_screenRects.size(); i++) {
        if (PtInRect(&g_screenRects[i], mousePos)) {
            const RECT& sr = g_screenRects[i];
            int sw = sr.right - sr.left;
            int sh = sr.bottom - sr.top;
            int col = (mousePos.x - sr.left) * 26 / sw;
            int row = (mousePos.y - sr.top) * 26 / sh;
            if (col < 0) col = 0; if (col > 25) col = 25;
            if (row < 0) row = 0; if (row > 25) row = 25;
            out[0] = 'A' + col;
            out[1] = 'A' + row;
            return;
        }
    }
}

static HFONT g_coordFont = NULL;
static HFONT g_coordFontBig = NULL;
static HBRUSH g_coordBgBrush = NULL;
void EndClickFlash();  // 前向声明

#define TIMER_CLICK_FLASH 1001

LRESULT CALLBACK IndicatorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rect;
        GetClientRect(hwnd, &rect);

        if (!g_coordBgBrush) g_coordBgBrush = CreateSolidBrush(RGB(0, 0, 0));
        if (!g_coordFont) g_coordFont = CreateFont(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        if (!g_coordFontBig) g_coordFontBig = CreateFont(20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

        FillRect(hdc, &rect, g_coordBgBrush);

        POINT mousePos;
        GetCursorPos(&mousePos);
        char coord[3];
        GetHintCoord(mousePos, coord);

        SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ oldFont = SelectObject(hdc, g_clickFlash ? g_coordFontBig : g_coordFont);

        // 分别绘制两个字母，不同颜色
        char c1[2] = { coord[0], '\0' };
        char c2[2] = { coord[1], '\0' };

        if (g_clickFlash) {
            // 点击闪烁：两个字母都变黄
            SetTextColor(hdc, RGB(255, 230, 50));
            DrawTextA(hdc, coord, 2, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            // 正常：左半绿，右半青
            RECT r1 = rect;
            r1.right = (rect.right - rect.left) / 2;
            RECT r2 = rect;
            r2.left = r1.right;

            SetTextColor(hdc, RGB(50, 255, 120));   // 亮绿（列）
            DrawTextA(hdc, c1, 1, &r1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SetTextColor(hdc, RGB(255, 160, 40));   // 亮橙（行）
            DrawTextA(hdc, c2, 1, &r2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        SelectObject(hdc, oldFont);
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_USER + 200: {
        // 移动线程请求更新位置 (wParam=x, lParam=y)
        int cx = (int)wParam;
        int cy = (int)lParam;
        MoveWindow(hwnd, cx + 12, cy + 12, 22, 16, TRUE);
        InvalidateRect(hwnd, NULL, TRUE);
        break;
    }
    case WM_TIMER:
        if (wParam == TIMER_CLICK_FLASH) {
            KillTimer(hwnd, TIMER_CLICK_FLASH);
            EndClickFlash();
        }
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}


POINT g_lastMousePoint; // 假设已在全局定义

// 辅助函数：计算两点间欧几里得距离的平方（避免开方，提高效率）
inline long DistanceSquared(POINT a, POINT b) {
    long dx = a.x - b.x;
    long dy = a.y - b.y;
    return dx * dx + dy * dy;
}



// 触发点击状态显示
void TriggerClickFlash() {
    if (!g_indicatorWindow) return;
    g_clickFlash = true;
    POINT mousePos;
    GetCursorPos(&mousePos);
    MoveWindow(g_indicatorWindow, mousePos.x + 8, mousePos.y + 8, 30, 22, TRUE);
    ShowWindow(g_indicatorWindow, SW_SHOW);
    InvalidateRect(g_indicatorWindow, NULL, TRUE);
}

// 结束点击状态显示
void EndClickFlash() {
    if (!g_indicatorWindow) return;
    g_clickFlash = false;
    UpdateIndicatorPosition();
}

// 更新坐标指示器位置
void UpdateIndicatorPosition() {
    if (g_indicatorWindow && g_isActive && !g_hintMode && !g_gridMode) {
        POINT mousePos;
        GetCursorPos(&mousePos);

        if (g_clickFlash) {
            InvalidateRect(g_indicatorWindow, NULL, TRUE);
        } else {
            // SetWindowPos 同时更新位置和 Z-order，确保在任务栏之上
            SetWindowPos(g_indicatorWindow, HWND_TOPMOST,
                mousePos.x + 12, mousePos.y + 12, 22, 16,
                SWP_NOACTIVATE | SWP_SHOWWINDOW);
            InvalidateRect(g_indicatorWindow, NULL, TRUE);
        }
    }
    else if (g_indicatorWindow) {
        ShowWindow(g_indicatorWindow, SW_HIDE);
    }
}

// 创建Tag窗口
void CreateTagWindow(int x, int y, char letter) {
    RemoveTagBySameLetter(letter);
    // 注册tag窗口类
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = TagWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"TagWindowClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassEx(&wc);

    // 创建tag窗口
    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,  // 置顶且支持透明
        L"TagWindowClass",
        NULL,
        WS_POPUP,
        x-15, y-15, 30, 30,  // 位置和大小
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (hwnd) {
        // 设置窗口透明度 (半透)
        SetLayeredWindowAttributes(hwnd, 0, g_tagMode ? 255 : 150, LWA_ALPHA);
        ShowWindow(hwnd, SW_SHOW);

        // 添加到标签列表
        TagInfo tag;
        tag.hwnd = hwnd;
        tag.pos.x = x;
        tag.pos.y = y;
        tag.letter = letter;
        tag.active = true;
        g_tags.push_back(tag);

        // 保存到配置文件
        SaveTagsToConfig();
    }
}

// 移除Tag窗口
void RemoveTagWindow(int x, int y) {
    for (auto it = g_tags.begin(); it != g_tags.end(); ++it) {
        if (it->pos.x == x && it->pos.y == y) {
            // 销毁窗口
            if (it->hwnd) {
                DestroyWindow(it->hwnd);
            }
            // 从列表中移除
            g_tags.erase(it);
            // 保存到配置文件
            SaveTagsToConfig();
            ExitTagMode();
            break;
        }
    }
}

void RemoveTagWindowByLetter(char letter) {
    for (auto it = g_tags.begin(); it != g_tags.end(); ++it) {
        if (it->letter == letter) {
            // 销毁窗口
            if (it->hwnd) {
                DestroyWindow(it->hwnd);
            }
            // 从列表中移除
            g_tags.erase(it);
            // 保存到配置文
            SaveTagsToConfig();
            break;
        }
    }
}

// 进入tag模式
void EnterTagMode() {
    //if (g_tagMode) return;
    //DebugLog("enter tag");

    g_tagMode = true;
    ShowTagWindowsInteractive();

    // 更新所有标签窗口的透明度
    for (auto& tag : g_tags) {
        if (tag.hwnd) {
            SetLayeredWindowAttributes(tag.hwnd, 0, 255, LWA_ALPHA); // 不透明
            InvalidateRect(tag.hwnd, NULL, TRUE); // 重绘
        }
    }
}

// 退出tag模式
void ExitTagMode() {
    //if (!g_tagMode) return;

    //DebugLog("EXIT");
    g_tagMode = false;

    // 更新所有标签窗口的透明度
    for (auto& tag : g_tags) {
        if (tag.hwnd) {
            SetLayeredWindowAttributes(tag.hwnd, 0, 150, LWA_ALPHA); // 半透
            InvalidateRect(tag.hwnd, NULL, TRUE); // 重绘
        }
    }
    ShowTagWindowsNonInteractive();
    g_dotSize = 8;
    UpdateIndicatorPosition();
}

// 跳转到标签位置
bool JumpToTag(char letter) {
    for (const auto& tag : g_tags) {
        if (tag.letter == letter) {
            SetCursorPos(tag.pos.x, tag.pos.y);
            ExitTagMode();
            return true;
            break;
        }
    }
    return false;
}

// 创建grid窗口
void CreateGridWindow() {
    // 注册grid窗口类
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = GridWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"GridWindowClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassEx(&wc);

    // 创建grid窗口，初始时隐藏
    g_gridWindow = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED,  // 置顶且支持透明
        L"GridWindowClass",
        NULL,
        WS_POPUP,
        0, 0, 0, 0,  // 初始位置设为0,0，大小为0,0
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (g_gridWindow) {
        // 设置窗口透明度
        SetLayeredWindowAttributes(g_gridWindow, 0, 150, LWA_ALPHA);
        ShowWindow(g_gridWindow, SW_HIDE);
    }
}

// 创建hint窗口
void CreateHintWindow() {
    // 注册hint窗口类
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = HintWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"HintWindowClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassEx(&wc);

    // 创建单个窗口，初始时隐藏
    g_hintWindow = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,  // 置顶且支持透明
        L"HintWindowClass",
        NULL,
        WS_POPUP,
        0, 0, 0, 0,  // 初始位置设为0,0，大小为0,0
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (g_hintWindow) {
        // 设置窗口透明度
        SetLayeredWindowAttributes(g_hintWindow, 0, 100, LWA_ALPHA);
        ShowWindow(g_hintWindow, SW_HIDE);
    }
}

// 创建指示器窗口
void CreateIndicatorWindow() {
    // 注册指示器窗口类
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = IndicatorWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"IndicatorWndClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassEx(&wc);

    // 创建坐标指示器窗口（显示 hint 坐标字母）
    g_indicatorWindow = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        L"IndicatorWndClass",
        NULL,
        WS_POPUP,
        0, 0, 22, 16,  // 22x16 显示两个字母
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (g_indicatorWindow) {
        // 设置窗口透明度
        SetLayeredWindowAttributes(g_indicatorWindow, 0, 200, LWA_ALPHA);

        // 初始时隐藏指示器
        ShowWindow(g_indicatorWindow, SW_HIDE);
    }
}




// 进入hint模式
void EnterHintMode() {
    if (g_hintMode) return;  // 已经在hint模式中

    g_hintMode = true;
    g_currentHint = "";
    g_hintScreenIndex = GetCurrentScreenIndex();
    //DebugLog("hint index %d", g_hintScreenIndex);

    // 检查是否有屏幕信息
    if (g_screenRects.empty()) {
        // 如果没有枚举到屏幕，使用主屏幕
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        g_hintScreenIndex = 0;

        OutputDebugStringA("Blocked 'M' key\n"); // 安全！
        // 创建一个虚拟的屏幕矩形
        RECT mainScreenRect = { 0, 0, screenWidth, screenHeight };
        g_screenRects.push_back(mainScreenRect);
    }

    if (g_hintScreenIndex >= 0 && g_hintScreenIndex < (int)g_screenRects.size()) {
        const RECT& screenRect = g_screenRects[g_hintScreenIndex];

        // 更新窗口位置和大小
        MoveWindow(
            g_hintWindow,
            screenRect.left,
            screenRect.top,
            screenRect.right - screenRect.left,
            screenRect.bottom - screenRect.top,
            TRUE
        );
    }

    ShowWindow(g_hintWindow, SW_SHOW);
    UpdateWindow(g_hintWindow);

    // 隐藏指示器
    if (g_indicatorWindow) {
        ShowWindow(g_indicatorWindow, SW_HIDE);
    }
}


// 退出hint模式
void ExitHintMode(bool isShowSubGrid = true) {
    if (!g_hintMode) return;

    g_hintMode = false;
    g_currentHint = "";

    ShowWindow(g_hintWindow, SW_HIDE);

    g_dotSize = 8;
    // 恢复指示器
    UpdateIndicatorPosition();

    // 退出Hint状态时，鼠标速度调整为15
    g_mouseSpeed = 15;
    g_lastSetSpeed = 15;

    if (isShowSubGrid) {
        g_miniGridMode = true;
        EnterGridModeFromCurrentPos();
    }
}

// 退出滚轮模式
void ExitWheelMode() {
    if (!g_wheelMode) return;

    g_wheelMode = false;

    // 恢复指示器
    UpdateIndicatorPosition();
}

// 进入grid模式（以当前鼠标位置为中心，大小匹配hint格子）
void EnterGridModeFromCurrentPos() {
    if (g_gridMode) return;

    g_gridMode = true;
    g_gridStack.clear();

    POINT currentPos;
    GetCursorPos(&currentPos);

    // 根据当前屏幕计算 hint 格子大小
    int screenIdx = GetCurrentScreenIndex();
    const RECT& sr = g_screenRects[screenIdx];
    int regionWidth = (sr.right - sr.left) / 26;
    int regionHeight = (sr.bottom - sr.top) / 26;
    RECT region = {
        currentPos.x - regionWidth / 2,
        currentPos.y - regionHeight / 2,
        currentPos.x + regionWidth / 2,
        currentPos.y + regionHeight / 2
    };

    // 添加到区域栈
    g_gridStack.push_back(region);

    // 更新grid窗口位置和大小
    MoveWindow(
        g_gridWindow,
        region.left,
        region.top,
        region.right - region.left,
        region.bottom - region.top,
        TRUE
    );

    ShowWindow(g_gridWindow, SW_SHOW);
    UpdateWindow(g_gridWindow);

    // 隐藏指示器
    if (g_indicatorWindow) {
        ShowWindow(g_indicatorWindow, SW_HIDE);
    }
}

// 进入grid模式（以屏幕中央开始）
void EnterGridMode() {
    if (g_gridMode) return;  // 已经在grid模式中

    g_gridMode = true;
    g_gridStack.clear();

    // 获取当前屏幕区域
    int currentScreenIndex = GetCurrentScreenIndex();
    if (currentScreenIndex >= 0 && currentScreenIndex < (int)g_screenRects.size()) {
        RECT screenRect = g_screenRects[currentScreenIndex];
        g_gridStack.push_back(screenRect);

        // 将鼠标移动到屏幕中央
        int centerX = screenRect.left + (screenRect.right - screenRect.left) / 2;
        int centerY = screenRect.top + (screenRect.bottom - screenRect.top) / 2;
        SetCursorPos(centerX, centerY);

        // 更新grid窗口位置和大小
        MoveWindow(
            g_gridWindow,
            screenRect.left,
            screenRect.top,
            screenRect.right - screenRect.left,
            screenRect.bottom - screenRect.top,
            TRUE
        );

        ShowWindow(g_gridWindow, SW_SHOW);
        UpdateWindow(g_gridWindow);
    }

    // 隐藏指示器
    if (g_indicatorWindow) {
        ShowWindow(g_indicatorWindow, SW_HIDE);
    }
}

// 退出grid模式
void ExitGridMode() {
    if (!g_gridMode) return;

    g_gridMode = false;
    g_miniGridMode = false;
    g_gridCustomCenter = false;
    g_gridStack.clear();

    ShowWindow(g_gridWindow, SW_HIDE);

    g_dotSize = 8;
    // 恢复指示器
    UpdateIndicatorPosition();
}

// 在grid模式下移动到指定区域
void MoveToGridArea(int direction) {
    if (g_gridStack.empty()) return;

    RECT currentRect = g_gridStack.back();

    int midX, midY;
    if (g_gridCustomCenter) {
        midX = g_gridCenter.x;
        midY = g_gridCenter.y;
        // 限制在区域内
        if (midX < currentRect.left) midX = currentRect.left;
        if (midX > currentRect.right) midX = currentRect.right;
        if (midY < currentRect.top) midY = currentRect.top;
        if (midY > currentRect.bottom) midY = currentRect.bottom;
        // 使用一次后恢复默认中心
        g_gridCustomCenter = false;
    } else {
        midX = currentRect.left + (currentRect.right - currentRect.left) / 2;
        midY = currentRect.top + (currentRect.bottom - currentRect.top) / 2;
    }

    RECT newRect;
    int centerX, centerY;

    switch (direction) {
    case 'H': // 左半边区域 (左上 + 左下)
        newRect = { currentRect.left, currentRect.top, midX, currentRect.bottom };
        centerX = newRect.left + (newRect.right - newRect.left) / 2;
        centerY = newRect.top + (newRect.bottom - newRect.top) / 2;
        break;
    case 'J': // 下半边区域 (左下 + 右下)
        newRect = { currentRect.left, midY, currentRect.right, currentRect.bottom };
        centerX = newRect.left + (newRect.right - newRect.left) / 2;
        centerY = newRect.top + (newRect.bottom - newRect.top) / 2;
        break;
    case 'K': // 上半边区域 (左上 + 右上)
        newRect = { currentRect.left, currentRect.top, currentRect.right, midY };
        centerX = newRect.left + (newRect.right - newRect.left) / 2;
        centerY = newRect.top + (newRect.bottom - newRect.top) / 2;
        break;
    case 'L': // 右半边区域 (右上 + 右下)
        newRect = { midX, currentRect.top, currentRect.right, currentRect.bottom };
        centerX = newRect.left + (newRect.right - newRect.left) / 2;
        centerY = newRect.top + (newRect.bottom - newRect.top) / 2;
        break;
    default:
        return;
    }

    // 将鼠标移动到新区域中央
    SetCursorPos(centerX, centerY);

    // 添加到区域栈
    g_gridStack.push_back(newRect);

    // 更新grid窗口位置和大小
    MoveWindow(
        g_gridWindow,
        newRect.left,
        newRect.top,
        newRect.right - newRect.left,
        newRect.bottom - newRect.top,
        TRUE
    );

    ShowWindow(g_gridWindow, SW_SHOW);
    InvalidateRect(g_gridWindow, NULL, TRUE);
    UpdateWindow(g_gridWindow);
}

// 在grid模式下快速定位到角落区域
void MoveToGridCorner(int corner) {
    if (g_gridStack.empty()) return;

    RECT currentRect = g_gridStack.back();

    int midX, midY;
    if (g_gridCustomCenter) {
        midX = g_gridCenter.x;
        midY = g_gridCenter.y;
        if (midX < currentRect.left) midX = currentRect.left;
        if (midX > currentRect.right) midX = currentRect.right;
        if (midY < currentRect.top) midY = currentRect.top;
        if (midY > currentRect.bottom) midY = currentRect.bottom;
        g_gridCustomCenter = false;
    } else {
        midX = currentRect.left + (currentRect.right - currentRect.left) / 2;
        midY = currentRect.top + (currentRect.bottom - currentRect.top) / 2;
    }

    RECT newRect;
    int centerX, centerY;

    switch (corner) {
    case 'Q': // 左上角区域
        newRect = { currentRect.left, currentRect.top, midX, midY };
        centerX = newRect.left + (newRect.right - newRect.left) / 2;
        centerY = newRect.top + (newRect.bottom - newRect.top) / 2;
        break;
    case 'W': // 右上角区域
        newRect = { midX, currentRect.top, currentRect.right, midY };
        centerX = newRect.left + (newRect.right - newRect.left) / 2;
        centerY = newRect.top + (newRect.bottom - newRect.top) / 2;
        break;
    case 'A': // 左下角区域
        newRect = { currentRect.left, midY, midX, currentRect.bottom };
        centerX = newRect.left + (newRect.right - newRect.left) / 2;
        centerY = newRect.top + (newRect.bottom - newRect.top) / 2;
        break;
    case 'S': // 右下角区域
        newRect = { midX, midY, currentRect.right, currentRect.bottom };
        centerX = newRect.left + (newRect.right - newRect.left) / 2;
        centerY = newRect.top + (newRect.bottom - newRect.top) / 2;
        break;
    default:
        return;
    }

    // 将鼠标移动到角落区域中央
    SetCursorPos(centerX, centerY);

    // 添加到区域栈
    g_gridStack.push_back(newRect);

    // 更新grid窗口位置和大小
    MoveWindow(
        g_gridWindow,
        newRect.left,
        newRect.top,
        newRect.right - newRect.left,
        newRect.bottom - newRect.top,
        TRUE
    );

    ShowWindow(g_gridWindow, SW_SHOW);
    InvalidateRect(g_gridWindow, NULL, TRUE);
    UpdateWindow(g_gridWindow);
}

// 返回上一个区域
void ReturnToPreviousGrid() {
    if (g_gridStack.size() <= 1) {
        // 如果只有一个区域，退出grid模式
        ExitGridMode();
        return;
    }

    // 移除当前区域
    g_gridStack.pop_back();

    // 获取上一个区域
    if (!g_gridStack.empty()) {
        RECT prevRect = g_gridStack.back();

        // 将鼠标移动到上一个区域中央
        int centerX = prevRect.left + (prevRect.right - prevRect.left) / 2;
        int centerY = prevRect.top + (prevRect.bottom - prevRect.top) / 2;
        SetCursorPos(centerX, centerY);

        // 更新grid窗口位置和大小
        MoveWindow(
            g_gridWindow,
            prevRect.left,
            prevRect.top,
            prevRect.right - prevRect.left,
            prevRect.bottom - prevRect.top,
            TRUE
        );

        ShowWindow(g_gridWindow, SW_SHOW);
        UpdateWindow(g_gridWindow);
    }
}



// 钩子回调函数
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* pKeyboard = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vkCode = pKeyboard->vkCode;
        bool isKeyDown = (wParam == WM_KEYDOWN);
        bool isKeyUp = (wParam == WM_KEYUP);
        g_iskeyDown = isKeyDown;
        // 更新Ctrl键状态
        if (vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL) {
            g_ctrlPressed = isKeyDown;
            return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
        }
        // 更新Shift键状态
        if (vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT) {
            g_shiftPressed = isKeyDown;
            return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
        }
        if(vkCode == VK_UP || vkCode == VK_DOWN || vkCode == VK_LEFT || vkCode == VK_RIGHT)
        {
            return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
		}

   //     if (vkCode == VK_LMENU || vkCode == VK_RMENU)
   //     {
   //         DebugLog("alt first %d \n",isKeyDown);
   //         g_altPressed = isKeyDown;
			//return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
   //     }
        g_altPressed =  (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

        if (g_ctrlPressed && vkCode == VK_OEM_MINUS)
        {
        
            //不处理vs studio的状态
            return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
        }

        // 更新Win键状态
        if (vkCode == VK_LWIN || vkCode == VK_RWIN) {
            g_winPressed = isKeyDown;
            return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
        }

        // 检查 Win+空格 组合键，确保不被屏蔽
        if (g_winPressed && vkCode != VK_LWIN && vkCode != VK_RWIN) {
            // 不阻止Win+空格，让系统正常处理
            return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
        }

        g_dotSize = 1;
        //log isKeyDown vkCode and g_altPressed
       // 在 LowLevelKeyboardProc 中合适的位置（比如 nCode >= 0 且处理按键时）
        //DebugLog(
        //    "Debug: isKeyDown=%d, vkCode=%lu (0x%02X), g_altPressed=%d, g_ctralpressed=%d "
        //    "is J? %d, is K? %d, condition=%d\n",
        //    isKeyDown,
        //    (DWORD)vkCode,
        //    (DWORD)vkCode,
        //    g_altPressed,
        //    g_ctrlPressed,
        //    (vkCode == 'J'),
        //    (vkCode == 'K'),
        //    (isKeyDown && (vkCode == 'J' || vkCode == 'K') && g_altPressed)
        //);
        // 检查 Ctrl+\ 切换激活状态
        if (isKeyDown && ((vkCode == 'J' && g_ctrlPressed) || (vkCode == 'K' && g_ctrlPressed && g_altPressed))) {
            g_isActive = !g_isActive;
            if (g_isActive) {
                SetVimouseCursor();
                ShowTagWindowsNonInteractive();
                g_currentScreenIndex = GetCurrentScreenIndex();
                if (vkCode == 'K')
                {
                    const RECT& screenRect = g_screenRects[g_currentScreenIndex];
                    int centerX = screenRect.left + (screenRect.right - screenRect.left) / 2;
                    int centerY = screenRect.top + (screenRect.bottom - screenRect.top) / 2;
                    SetCursorPos(centerX, centerY);
                }

                GetCursorPos(&g_lastMousePos);  // 记录当前位置
                // 重置C键状态
                g_firstCPress = true;
                g_lastActionWasC = false;
                // 重置鼠标按键状态
                g_leftButtonDown = false;
                g_rightButtonDown = false;
                g_wheelMode = false;  // 退出滚轮模式
                g_gridMode = false;  // 退出grid模式

                g_tagMode = false;
                g_altPressed = false;
                // 重置Ctrl状态
                g_ctrlPressed = false;
                // 重置Win状态
                g_winPressed = false;
                // 重新枚举显示器
                g_screenRects.clear();
                EnumDisplayMonitors(NULL, NULL, EnumDisplayMonitorsProc, 0);
            } else {
                RestoreSystemCursor();
            }
            g_dotSize = 5;
            // 更新指示器位置
            UpdateIndicatorPosition();
            return 1;
        }

        GetCursorPos(&g_lastMousePos);  // 记录当前位置

        // 检查 Enter 关闭激活状态,并且在当前区域点击一下鼠标
        if (isKeyDown && vkCode == VK_RETURN && g_isActive) {
            g_isActive = false;
            RestoreSystemCursor();
            ExitHintMode(false);  // 退出hint模式
            ExitWheelMode();  // 退出滚轮模式
            ExitGridMode();   // 退出grid模式
            ExitTagMode();
            HideAllTagWindows();
            // 更新指示器位置
            g_leftButtonDown = true;  // 设置左键按下状态
            // 更新指示器位置（会触发重绘）
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            g_leftButtonDown = false;  // 设置左键抬起状态

            UpdateIndicatorPosition();
            if (g_ctrlPressed) {
                return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);  // 让Esc正常工作
            }
            return 1;

        }


        // 检查 Esc 关闭激活状态
        if (isKeyDown && vkCode == VK_ESCAPE && g_isActive) {
            if (g_hintMode) {
                ExitHintMode(false);  // 退出hint模式
                return 1;
            }
            if (g_gridMode)
            {
                ExitGridMode();
                return 1;
            }
            if (g_wheelMode)
            {
                ExitWheelMode();
                return 1;
            }

            g_isActive = false;
            RestoreSystemCursor();
            ExitWheelMode();  // 退出滚轮模式
            ExitGridMode();   // 退出grid模式
            ExitTagMode();
            HideAllTagWindows();
            // 更新指示器位置
            UpdateIndicatorPosition();
            return 1;
            //return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);  // 让Esc正常工作
        }

        // 如果不处于激活状态，直接返回
        if (!g_isActive) {
            HideAllTagWindows();
            return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
        }

        if (isKeyDown && vkCode >= '0' && vkCode <= '9' && !g_ctrlPressed && !g_winPressed)
        {
            DebugLog("presed %c", vkCode);
            g_tagTabIndex = vkCode - '0';
			return 1;
        }

        if (g_tagMode)
        {
            if(isKeyDown && !g_ctrlPressed && !g_winPressed && !g_altPressed)
            {
                if (vkCode >= 'A' && vkCode <= 'Z' && vkCode != 'W' && vkCode != 'Q')
                {
                    bool jsucces = JumpToTag((char)vkCode);
                    if (jsucces)
                    {
						return 1;
                    }
					ExitTagMode();
                }
			}
        }

        // 如果处于grid模式，处理特定键
        if (g_gridMode) {
            if (isKeyDown) {
                switch (vkCode) {
                case 'H':  // 左半（vim: 左）
                case 'J':  // 下半（vim: 下）
                case 'K':  // 上半（vim: 上）
                case 'L':  // 右半（vim: 右）
                    MoveToGridArea(vkCode);
                    if (g_miniGridMode) { g_miniGridMode = false; ExitGridMode(); }
                    return 1;
                case 'U':  // 左上角（对角键）
                    MoveToGridCorner('Q');
                    if (g_miniGridMode) { g_miniGridMode = false; ExitGridMode(); }
                    return 1;
                case 'O':  // 右上角（对角键）
                    MoveToGridCorner('W');
                    if (g_miniGridMode) { g_miniGridMode = false; ExitGridMode(); }
                    return 1;
                case 'N':  // 左下角（对角键）
                    MoveToGridCorner('A');
                    if (g_miniGridMode) { g_miniGridMode = false; ExitGridMode(); }
                    return 1;
                case VK_OEM_PERIOD:  // 右下角（对角键 .）
                    MoveToGridCorner('S');
                    if (g_miniGridMode) { g_miniGridMode = false; ExitGridMode(); }
                    return 1;
                case 'R':  // 返回上一个区域
                    ReturnToPreviousGrid();
                    return 1;
                case 'I':  // 退出grid模式（拦截，不传递）
                    ExitGridMode();
                    return 1;
                case VK_ESCAPE:  // 退出grid模式（Esc 传递给系统）
                    ExitGridMode();
                    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                case 'T':
                 
                    // 退出Grid模式
                    if (!g_leftButtonDown) {
                        ExitGridMode();
                        g_leftButtonDown = false;  // 设置左键按下状态
                        // 更新指示器位置（会触发重绘）
                        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                        PutTag(g_lastMousePos);
                        UpdateIndicatorPosition();
                    }
                    return 1;
                case 'F':  // 左键点击
                {
                    // 在Grid模式下执行点击后，保持当前位置并退出Grid模式

                    // 退出Grid模式
                    if (!g_leftButtonDown) {
                        ExitGridMode();
                        g_leftButtonDown = true;  // 设置左键按下状态
                        // 更新指示器位置（会触发重绘）
                        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                        UpdateIndicatorPosition();
                    }
                    return 1;
                }
                return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);  // 让F键正常工作
                case 'G':  // 右键点击
                {
                    // 在Grid模式下执行点击后，保持当前位置并退出Grid模式
                    g_rightButtonDown = true;  // 设置右键按下状态
                    // 更新指示器位置（会触发重绘）
                    UpdateIndicatorPosition();

                    // 退出Grid模式
                    ExitGridMode();
                    mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
                    g_rightButtonDown = false;  // 设置右键抬起状态
                    // 更新指示器位置（会触发重绘）
                    UpdateIndicatorPosition();
                }
                return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);  // 让G键正常工作
                case 'V':  // 拖拽切换
                {
                    // 退出Grid模式
                    ExitGridMode();
                    // 在Grid模式下执行拖拽操作后，保持当前位置并退出Grid模式
                    if (!g_isDragging) {
                        // 开始拖动
                        GetCursorPos(&g_lastMousePos);
                        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                        g_isDragging = true;
                    }
                    else {
                        // 结束拖动
                        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                        g_isDragging = false;
                    }
                    // 更新指示器位置
                    UpdateIndicatorPosition();
                }
                return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);  // 让V键正常工作
                default:
                    // 其他键退出grid模式并继续处理
                    ExitGridMode();
                    return 1;
                    //return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                }
            }
        }

        // 如果处于hint模式，只处理字母键和Esc键
        if (g_hintMode) {
            if (isKeyDown) {
                if ((vkCode >= 'A' && vkCode <= 'Z') || vkCode == VK_ESCAPE) {
                    if (vkCode == VK_ESCAPE) {
                        // Esc键退出hint模式
                        g_hintMode = false;
                        g_currentHint = "";
                        ShowWindow(g_hintWindow, SW_HIDE);
                        // 恢复指示器
                        UpdateIndicatorPosition();
                        // 退出Hint状态时，鼠标速度调整为15
                        g_mouseSpeed = 15;
                        g_lastSetSpeed = 15;
                        // 在鼠标当前位置进入Grid模式，创建180x100的子grid区域
                        EnterGridModeFromCurrentPos();
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);  // 让Esc正常工作
                    }
                    else {
                        // 处理字母键
                        //char letter = (char)(vkCode - 'A' + 'a');  // 转换为小写
                        char letter = vkCode;

                        if (g_currentHint.length() == 0) {
                            // 第一个字母，更新显示（只显示匹配列）
                            g_currentHint += letter;
                            InvalidateRect(g_hintWindow, NULL, TRUE);  // 触发重绘
                            return 1;
                        }
                        else if (g_currentHint.length() == 1) {
                            // 第二个字母，定位并退出hint模式
                            g_currentHint += letter;

                            // 检查数组边界
                            if (g_hintScreenIndex >= 0 && g_hintScreenIndex < (int)g_screenRects.size()) {
                                // 找到对应的位置
                                int col = g_currentHint[0] - 'A';
                                int row = g_currentHint[1] - 'A';

                                const RECT& screenRect = g_screenRects[g_hintScreenIndex];
                                int blockWidth = (screenRect.right - screenRect.left) / 26;
                                int blockHeight = (screenRect.bottom - screenRect.top) / 26;

                                int x = screenRect.left + col * blockWidth + blockWidth / 2;
                                int y = screenRect.top + row * blockHeight + blockHeight / 2;

                                SetCursorPos(x, y);
                            }

                            // 退出hint模式
                            ExitHintMode(true);
                        }
                        return 1;
                    }
                }
                // 对于hint模式下非字母和Esc键，直接阻止
                return 1;
            }
        }
        else {
            // 非hint模式，处理其他按键
            if (isKeyDown) {
                // 检查 Ctrl+F 并执行鼠标左键点击
                if (g_ctrlPressed && vkCode == 'F') {
                    // 执行鼠标左键点击操作
                    g_leftButtonDown = true;  // 设置左键按下状态
                    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                    g_leftButtonDown = false;  // 设置左键抬起状态

                    // 更新指示器位置
                    UpdateIndicatorPosition();

                    // 阻止 Ctrl+F 传递给其他程序
                    return 1;
                }

                // 检查是否为Ctrl+字母组合键，如果是则传递给其他程序
                // 但只对C和V进行特殊处理，其他Ctrl+字母组合直接传递
                if (g_ctrlPressed && (vkCode >= 'A' && vkCode <= 'Z')) {
                    // 特殊处理：Ctrl+C 和 Ctrl+V 需要传递给其他程序
					return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                }

                // 滚轮模式处理：如果在滚轮模式下，HJKL用于滚轮滚动
                if (g_wheelMode) {
                    //GetCursorPos(&currentPos);

                    switch (vkCode) {
                    case 'H':  // 左移 -> 水平向左滚动
                        mouse_event(MOUSEEVENTF_HWHEEL, 0, 0, -g_wheelSpeed, 0);
                        break;
                    case 'J':  // 下移 -> 垂直向下滚动
                        mouse_event(MOUSEEVENTF_WHEEL, 0, 0, -g_wheelSpeed, 0);
                        break;
                    case 'K':  // 上移 -> 垂直向上滚动
                        mouse_event(MOUSEEVENTF_WHEEL, 0, 0, g_wheelSpeed, 0);
                        break;
                    case 'L':  // 右移 -> 水平向右滚动
                        mouse_event(MOUSEEVENTF_HWHEEL, 0, 0, g_wheelSpeed, 0);
                        break;
                    case 'Y':  // 再次按U键退出滚轮模式
                        ExitWheelMode();
                        break;
                    case 'M':  // 按M键进入hint模式，退出滚轮模式
                        ExitWheelMode();
                        EnterHintMode();
                        break;
                        // 速度键：QWER 不退出滚轮模式，但改变速度
                    case 'Q':
                        // QW键功能留空
                        break;
                    case 'W':
                        // QW键功能留空
                        break;
                    case 'E':
                        g_mouseSpeed = 160;
                        g_lastSetSpeed = 160;
                        break;
                    case 'R':
                        g_mouseSpeed = 320;
                        g_lastSetSpeed = 320;
                        break;
                    case 'F':  // 左键点击
                        g_leftButtonDown = true;  // 设置左键按下状态
                        // 更新指示器位置（会触发重绘）
                        UpdateIndicatorPosition();
                        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                        g_leftButtonDown = false;  // 设置左键抬起状态
                        // 更新指示器位置（会触发重绘）
                        UpdateIndicatorPosition();
                        break;
                    case 'G':  // 右键点击
                        g_rightButtonDown = true;  // 设置右键按下状态
                        // 更新指示器位置（会触发重绘）
                        UpdateIndicatorPosition();
                        mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
                        g_rightButtonDown = false;  // 设置右键抬起状态
                        // 更新指示器位置（会触发重绘）
                        UpdateIndicatorPosition();
                        break;
                    case 'V':
                        if (!g_isDragging) {
                            // 开始拖动
                            GetCursorPos(&g_lastMousePos);
                            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                            g_isDragging = true;
                        }
                        else {
                            // 结束拖动
                            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                            g_isDragging = false;
                        }
                        // 更新指示器位置
                        UpdateIndicatorPosition();
                        break;
                    default:
                        // 其他按键退出滚轮模式并处理为普通按键
                        ExitWheelMode();
                        // 重新处理这个按键
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }

                    // 更新指示器位置（会触发重绘）
                    UpdateIndicatorPosition();

                    // 阻止其他程序接收到这些按键
                    return 1;
                }

                UpdateIndicatorPosition();

                // 非滚轮模式下的按键处理
                //POINT currentPos;
                if (vkCode >= VK_NUMPAD0 && vkCode <= VK_NUMPAD9)
                {
					return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                }

                switch (vkCode) {
                case 'H':  // 左移（Shift = 精确1像素）
                    if (g_ctrlPressed) {
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    g_hPressed = true;
                    g_lastActionWasC = false;
                    StartSmoothMove();
                    UpdateIndicatorPosition();
                    break;
                case 'J':  // 下移（Shift = 精确1像素）
                    if (g_ctrlPressed) {
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    g_jPressed = true;
                    g_lastActionWasC = false;
                    StartSmoothMove();
                    UpdateIndicatorPosition();
                    break;
                case 'K':  // 上移（Shift = 精确1像素）
                    if (g_ctrlPressed) {
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    g_kPressed = true;
                    g_lastActionWasC = false;
                    StartSmoothMove();
                    UpdateIndicatorPosition();
                    break;
                case 'L':  // 右移（Shift = 精确1像素）
                    if (g_ctrlPressed) {
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    g_lPressed = true;
                    g_lastActionWasC = false;
                    StartSmoothMove();
                    UpdateIndicatorPosition();
                    break;

                case 'U':  // 左上移动
                    if (g_ctrlPressed) {
                        // Ctrl+U传递给其他程序
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    g_uPressed = true;
                    g_lastActionWasC = false;  // 重置C键状态
                    StartSmoothMove();
                    // 更新指示器位置
                    UpdateIndicatorPosition();
                    break;
                case 'O':  // 右上移动
                    if (g_ctrlPressed) {
                        // Ctrl+O传递给其他程序
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    g_oPressed = true;
                    g_lastActionWasC = false;  // 重置C键状态
                    StartSmoothMove();
                    // 更新指示器位置
                    UpdateIndicatorPosition();
                    break;
                case 'N':  // 左下移动
                    if (g_ctrlPressed) {
                        // Ctrl+N传递给其他程序
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    g_nPressed = true;
                    g_lastActionWasC = false;  // 重置C键状态
                    StartSmoothMove();
                    // 更新指示器位置
                    UpdateIndicatorPosition();
                    break;
                case VK_OEM_PERIOD:  // 右下移动 (句号键)
                    if (g_ctrlPressed) {
                        // Ctrl+句号传递给其他程序
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    g_dotPressed = true;  // 修改：使用g_dotPressed表示句号键
                    g_lastActionWasC = false;  // 重置C键状态
                    StartSmoothMove();
                    // 更新指示器位置
                    UpdateIndicatorPosition();
                    break;

                    // 滚轮模式
                case 'Y':
                    g_wheelMode = !g_wheelMode;  // 切换滚轮模式
                    if (g_wheelMode) {
                        // 进入滚轮模式
                        g_lastActionWasC = false;  // 重置C键状态
                    }
                    else {
                        // 退出滚轮模式
                        // 恢复指示器
                        UpdateIndicatorPosition();
                    }
                    break;

                    // Grid模式（第一次i=鼠标位置grid，grid中再按i=屏幕中心grid）
                case 'I':
                    if (g_ctrlPressed) {
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    if (g_gridMode) {
                        // 已在 grid 中：切换到屏幕中心 grid
                        ExitGridMode();
                        EnterGridMode();
                    } else {
                        // 进入鼠标位置 grid
                        g_gridMode = true;
                        g_gridStack.clear();
                        int si = GetCurrentScreenIndex();
                        if (si >= 0 && si < (int)g_screenRects.size()) {
                            RECT sr = g_screenRects[si];
                            g_gridStack.push_back(sr);
                            // 记录鼠标位置为自定义中心
                            GetCursorPos(&g_gridCenter);
                            g_gridCustomCenter = true;
                            MoveWindow(g_gridWindow, sr.left, sr.top,
                                sr.right - sr.left, sr.bottom - sr.top, TRUE);
                            ShowWindow(g_gridWindow, SW_SHOW);
                            InvalidateRect(g_gridWindow, NULL, TRUE);
                            UpdateWindow(g_gridWindow);
                        }
                        if (g_indicatorWindow) ShowWindow(g_indicatorWindow, SW_HIDE);
                    }
                    g_lastActionWasC = false;
                    break;

                    // 标签功能
                case 'Q':
                {
                    PutTag(g_lastMousePos);

                    return 1;
                }
                case 'W':
                    if (g_ctrlPressed) {
                        // 如果W键是和Ctrl一起按下的，不执行W键功能，直接传递给其他程序
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    g_lastActionWasC = false;  // 重置C键状态
                    if (g_tagMode)
                    {
                        g_tagMode =false;
                        ExitTagMode();
                        break;
                    }
                    // 按W键进入tag模式
                    EnterTagMode();
                    break;
                case 'T':
                    if (!g_leftButtonDown) {
                        g_leftButtonDown = false;  // 设置左键按下状态
                        // 更新指示器位置（会触发重绘）
                        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
						std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                        UpdateIndicatorPosition();
                        AddMousePositionToStack();
                        PutTag(g_lastMousePos);
                        g_lastActionWasC = false;  // 重置C键状态
                    }
                    return 1;

                    // 鼠标点击
                case 'F':  // 左键点击（支持 Shift+F = Shift+Click, 保留 Ctrl+Click 传递）
                    if (!g_leftButtonDown) {
                        if (g_shiftPressed) keybd_event(VK_SHIFT, 0, 0, 0);
                        g_leftButtonDown = true;
                        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                        TriggerClickFlash();
                        AddMousePositionToStack();
                        if (g_shiftPressed) keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
                        g_lastActionWasC = false;
                    }
                    return 1;
                    break;
                case 'G':  // 右键点击
                    g_rightButtonDown = true;
                    mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
                    g_rightButtonDown = false;
                    TriggerClickFlash();
                    SetTimer(g_indicatorWindow, TIMER_CLICK_FLASH, 300, NULL);
                    g_lastActionWasC = false;
                    break;
                case 'B':  // 中键点击
                    mouse_event(MOUSEEVENTF_MIDDLEDOWN, 0, 0, 0, 0);
                    std::this_thread::sleep_for(std::chrono::milliseconds(30));
                    mouse_event(MOUSEEVENTF_MIDDLEUP, 0, 0, 0, 0);
                    TriggerClickFlash();
                    SetTimer(g_indicatorWindow, TIMER_CLICK_FLASH, 300, NULL);
                    g_lastActionWasC = false;
                    break;

                    // 拖动控制
                case 'V':
                    if (g_ctrlPressed) {
                        // 如果V键是和Ctrl一起按下的，不执行V键功能，直接传递给其他程序
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    if (!g_isDragging) {
                        // 开始拖动
                        GetCursorPos(&g_lastMousePos);
                        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                        g_isDragging = true;
                    }
                    else {
                        // 结束拖动
                        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                        g_isDragging = false;
                    }
                    g_lastActionWasC = false;  // 重置C键状态
                    // 更新指示器位置
                    UpdateIndicatorPosition();
                    break;

                    // 屏幕中心控制
                case 'C':
                    if (g_ctrlPressed) {
                        // 如果C键是和Ctrl一起按下的，不执行C键功能，直接传递给其他程序
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    g_dotSize = 5;
                    if (g_screenRects.empty()) {
                        // 如果没有枚举到屏幕，使用主屏幕
                        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
                        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
                        SetCursorPos(screenWidth / 2, screenHeight / 2);
                    }
                    else {
                        if (g_firstCPress || !g_lastActionWasC) {
                            // 第一次按C或上次操作不是C：移动到当前屏幕中心
                            g_currentScreenIndex = GetCurrentScreenIndex();
                            g_firstCPress = false;
                            g_lastActionWasC = true;
                        }
                        else {
                            // 上次操作是C：切换到下一个屏幕
                            g_currentScreenIndex = (g_currentScreenIndex + 1) % g_screenRects.size();
                        }

                        const RECT& screenRect = g_screenRects[g_currentScreenIndex];
                        int centerX = screenRect.left + (screenRect.right - screenRect.left) / 2;
                        int centerY = screenRect.top + (screenRect.bottom - screenRect.top) / 2;
                        SetCursorPos(centerX, centerY);
                    }
                    // 更新指示器位置
                    UpdateIndicatorPosition();
                    break;

                    // Hint模式
                case 'M':
                    EnterHintMode();
                    g_lastActionWasC = false;  // 重置C键状态
                    break;
                case VK_BACK:  // Backspace键进入tag模式
                    EnterTagMode();
                    return 1;
                    break;
                case 'A':
                case VK_LEFT:
                case VK_UP:
                case VK_RIGHT:
                case VK_DOWN:
                case VK_F1:
                case VK_F2:
                case VK_F5:
                case VK_F11:
                case VK_F12:
                    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);

                case 'R':  // 回到上一个位置
                    if (!g_gridMode) {  // 只在非grid模式下执行
                        GoToPreviousPosition();
                        g_lastActionWasC = false;  // 重置C键状态
                    }
                    break;
                case 'E':  // 前进到下一个位置
                    if (!g_gridMode) {  // 只在非grid模式下执行
                        GoToNextPosition();
                        g_lastActionWasC = false;  // 重置C键状态
                    }
                    break;

                default:
                    g_lastActionWasC = false;  // 重置C键状态
                    // 更新指示器位置
                    UpdateIndicatorPosition();
                    break;
                }

                // 阻止其他程序接收到这些按键
                return 1;
            }
            else if (isKeyUp) {
                // 处理Backspace键释放（退出tag模式）
                if (vkCode == VK_BACK && g_tagMode) {
                    ExitTagMode();
                    return 1;
                }


                UpdateIndicatorPosition();
                // 处理按键释放
                switch (vkCode) {
                case 'H':
                    g_hPressed = false;
                    if (!g_hPressed && !g_jPressed && !g_kPressed && !g_lPressed && !g_uPressed && !g_oPressed && !g_nPressed && !g_dotPressed) {
                        StopSmoothMove();
                    }
                    break;
                case 'J':
                    g_jPressed = false;
                    if (!g_hPressed && !g_jPressed && !g_kPressed && !g_lPressed && !g_uPressed && !g_oPressed && !g_nPressed && !g_dotPressed) {
                        StopSmoothMove();
                    }
                    break;
                case 'K':
                    g_kPressed = false;
                    if (!g_hPressed && !g_jPressed && !g_kPressed && !g_lPressed && !g_uPressed && !g_oPressed && !g_nPressed && !g_dotPressed) {
                        StopSmoothMove();
                    }
                    break;
                case 'L':
                    g_lPressed = false;
                    if (!g_hPressed && !g_jPressed && !g_kPressed && !g_lPressed && !g_uPressed && !g_oPressed && !g_nPressed && !g_dotPressed) {
                        StopSmoothMove();
                    }
                    break;
                case 'U':
                    g_uPressed = false;
                    if (!g_hPressed && !g_jPressed && !g_kPressed && !g_lPressed && !g_uPressed && !g_oPressed && !g_nPressed && !g_dotPressed) {
                        StopSmoothMove();
                    }
                    break;
                case 'O':
                    g_oPressed = false;
                    if (!g_hPressed && !g_jPressed && !g_kPressed && !g_lPressed && !g_uPressed && !g_oPressed && !g_nPressed && !g_dotPressed) {
                        StopSmoothMove();
                    }
                    break;
                case 'N':
                    g_nPressed = false;
                    if (!g_hPressed && !g_jPressed && !g_kPressed && !g_lPressed && !g_uPressed && !g_oPressed && !g_nPressed && !g_dotPressed) {
                        StopSmoothMove();
                    }
                    break;
                case VK_OEM_PERIOD:  // 句号键释放
                    g_dotPressed = false;
                    if (!g_hPressed && !g_jPressed && !g_kPressed && !g_lPressed && !g_uPressed && !g_oPressed && !g_nPressed && !g_dotPressed) {
                        StopSmoothMove();
                    }
                    break;
                case 'F':
                    if (g_leftButtonDown)
                    {
                        g_leftButtonDown = false;
                        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                        EndClickFlash();
                    }
                    break;
                default:
                    return 0;

                }
            }
        }
    }

    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
}

void PutTag(POINT currentPos) {

    // 检查当前位置是否已有标签
    bool tagExists = false;
    for (auto it = g_tags.begin(); it != g_tags.end(); ++it) {
        if (it->pos.x == currentPos.x && it->pos.y == currentPos.y || DistanceSquared(currentPos, it->pos) < 600) {
            // 移除现有标签
            //DebugLog("distance squared: %d", DistanceSquared(currentPos, it->pos));
            DestroyWindow(it->hwnd);
            g_tags.erase(it);
            tagExists = true;
            break;
        }
    }

    if (!tagExists) {


        // 创建新标签
        CreateTagWindow(currentPos.x, currentPos.y, g_nextLetter);

        // 更新下一个字母
        //g_nextLetter = (g_nextLetter - 'A' + 1) % 26 + 'A';
        UpdateNextLetter();
    }
    SaveTagsToConfig();
}

void UpdateNextLetter()
{
    static const char AVAILABLE_LETTERS[] = {
        'A','B','C','D','E','F','G','H','I','J','K','L','M',
        'N','O','P','R','S','T','U','V','X','Y','Z'
    };
    static int lastIndex = 0; // 记住上次分配的位置
    const int NUM_LETTERS = 24;

    // 先尝试从 lastIndex 开始找空闲
    for (int i = 0; i < NUM_LETTERS; ++i)
    {
        int idx = (lastIndex + i) % NUM_LETTERS;
        char c = AVAILABLE_LETTERS[idx];
        if (std::none_of(g_tags.begin(), g_tags.end(),
            [c](const TagInfo& t) { return t.letter == c; }))
        {
            g_nextLetter = c;
            lastIndex = (idx + 1) % NUM_LETTERS;
            return;
        }
    }

    // 全部已用：回到 lastIndex（或强制用第一个）
    g_nextLetter = AVAILABLE_LETTERS[lastIndex];
    lastIndex = (lastIndex + 1) % NUM_LETTERS;
}

bool RemoveTagBySameLetter(char letter) {
    auto it = std::find_if(g_tags.begin(), g_tags.end(),
        [letter](const TagInfo& t) { return t.letter == letter; }
    );
    if (it != g_tags.end()) {
        if (it->hwnd) {
            DestroyWindow(it->hwnd);
        }
        g_tags.erase(it);
        return true;
    }
    return false;
}

// 创建一个显示在任务栏的窗口来接收消息循环
// 托盘图标数据
static NOTIFYICONDATA g_nid = {};

static void AddTrayIcon(HWND hwnd, HINSTANCE hInstance) {
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MOUSECONTROLLER));
    wcscpy_s(g_nid.szTip, L"Vimouse");
    Shell_NotifyIcon(NIM_ADD, &g_nid);
}

static void RemoveTrayIcon() {
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
}

static bool IsSystemChinese() {
    LANGID lang = GetUserDefaultUILanguage();
    return PRIMARYLANGID(lang) == LANG_CHINESE;
}

static void ShowHelpDialog(HWND hwnd) {
    if (IsSystemChinese()) {
        const wchar_t* helpText =
            L"Vimouse \u64CD\u4F5C\u6307\u5357\n"
            L"\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\n\n"
            L"\u3010\u6FC0\u6D3B/\u9000\u51FA\u3011Ctrl+J \u5207\u6362\u952E\u76D8\u9F20\u6807\u6A21\u5F0F\n\n"
            L"\u3010\u79FB\u52A8\u3011h/j/k/l = \u5DE6/\u4E0B/\u4E0A/\u53F3\n"
            L"\u3000\u3000\u3000  u/o/n/. = \u5DE6\u4E0A/\u53F3\u4E0A/\u5DE6\u4E0B/\u53F3\u4E0B\n"
            L"\u3000\u3000\u3000  \u957F\u6309\u81EA\u52A8\u52A0\u901F\n\n"
            L"\u3010\u70B9\u51FB\u3011c = \u5DE6\u952E  x = \u53F3\u952E  v = \u53CC\u51FB\n"
            L"\u3010\u62D6\u62FD\u3011d = \u5F00\u59CB/\u7ED3\u675F\u62D6\u62FD\n"
            L"\u3010\u6EDA\u8F6E\u3011w \u8FDB\u5165\u6EDA\u8F6E\u6A21\u5F0F (j/k \u4E0A\u4E0B\u6EDA)\n\n"
            L"\u3010\u6807\u7B7E\u3011p = \u5728\u5F53\u524D\u4F4D\u7F6E\u653E\u6807\u7B7E\n"
            L"\u3000\u3000\u3000  t = \u8FDB\u5165\u6807\u7B7E\u8DF3\u8F6C\u6A21\u5F0F\n\n"
            L"\u3010Hint\u3011f = \u5C4F\u5E55\u5750\u6807\u5FEB\u901F\u8DF3\u8F6C\n"
            L"\u3010Grid\u3011g = \u7F51\u683C\u4E8C\u5206\u5B9A\u4F4D\n\n"
            L"\u3010\u7BA1\u9053 IPC\u3011\\\\.\\pipe\\vimouse\n"
            L"  \u547D\u4EE4: move click rclick dclick drag\n"
            L"        scroll pos keypress type\n"
            L"        tags tag sleep help";
        MessageBox(hwnd, helpText, L"Vimouse \u5E2E\u52A9", MB_OK | MB_ICONINFORMATION);
    } else {
        const wchar_t* helpText =
            L"Vimouse Quick Guide\n"
            L"=======================\n\n"
            L"[Toggle] Ctrl+J to switch keyboard-mouse mode\n\n"
            L"[Move] h/j/k/l = Left/Down/Up/Right\n"
            L"       u/o/n/. = Diagonals\n"
            L"       Hold to accelerate\n\n"
            L"[Click] c = Left  x = Right  v = Double\n"
            L"[Drag]  d = Start/End drag\n"
            L"[Scroll] w = Scroll mode (j/k to scroll)\n\n"
            L"[Tags] p = Place tag at cursor\n"
            L"       t = Enter tag-jump mode\n\n"
            L"[Hint] f = Screen coordinate quick jump\n"
            L"[Grid] g = Grid bisect positioning\n\n"
            L"[Pipe IPC] \\\\.\\pipe\\vimouse\n"
            L"  Commands: move click rclick dclick drag\n"
            L"            scroll pos keypress type\n"
            L"            tags tag sleep help";
        MessageBox(hwnd, helpText, L"Vimouse Help", MB_OK | MB_ICONINFORMATION);
    }
}

static void ShowTrayMenu(HWND hwnd) {
    bool zh = IsSystemChinese();
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, g_isActive ? MF_CHECKED : MF_UNCHECKED, IDM_TRAY_TOGGLE,
        zh ? L"\u542F\u7528\u952E\u76D8\u63A7\u5236" : L"Enable Keyboard Control");
    AppendMenu(hMenu, MF_STRING, IDM_TRAY_HELP,
        zh ? L"\u64CD\u4F5C\u6307\u5357" : L"Quick Guide");
    AppendMenu(hMenu, g_helpVisible ? MF_CHECKED : MF_UNCHECKED, IDM_TRAY_HELPWIN,
        zh ? L"\u60AC\u6D6E\u5E2E\u52A9" : L"Help Overlay");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, IDM_TRAY_EXIT,
        zh ? L"\u9000\u51FA" : L"Exit");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        // 重新枚举显示器
        g_screenRects.clear();
        EnumDisplayMonitors(NULL, NULL, EnumDisplayMonitorsProc, 0);

        // 创建grid窗口
        CreateGridWindow();

        // 创建hint窗口
        CreateHintWindow();

        // 创建坐标指示器窗口（显示 hint 坐标字母）
        CreateIndicatorWindow();

        // 创建帮助悬浮窗
        CreateHelpWindow();

        // 加载标签配置
        LoadTagsFromConfig();

        // 启动 Named Pipe 服务端
        StartPipeServer();

        // 添加系统托盘图标
        AddTrayIcon(hwnd, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE));

        // 设置键盘钩子
        g_keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc,
            GetModuleHandle(NULL), 0);
        if (g_keyboardHook == NULL) {
            MessageBox(NULL, L"设置键盘钩子失败", L"错误", MB_OK | MB_ICONERROR);
            PostQuitMessage(1);
        }
        break;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            ShowTrayMenu(hwnd);
        } else if (lParam == WM_LBUTTONDBLCLK) {
            // 双击托盘图标切换启用状态
            g_isActive = !g_isActive;
            UpdateIndicatorPosition();
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_TRAY_TOGGLE:
            g_isActive = !g_isActive;
            UpdateIndicatorPosition();
            break;
        case IDM_TRAY_HELP:
            ShowHelpDialog(hwnd);
            break;
        case IDM_TRAY_HELPWIN:
            ToggleHelpWindow();
            break;
        case IDM_TRAY_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        break;

    case WM_DESTROY:
        // 移除托盘图标
        RemoveTrayIcon();
        // 停止 Pipe 服务端
        StopPipeServer();
        // 停止平滑移动
        StopSmoothMove();

        // 卸载钩子
        if (g_keyboardHook) {
            UnhookWindowsHookEx(g_keyboardHook);
        }
        // 销毁grid窗口
        if (g_gridWindow) {
            DestroyWindow(g_gridWindow);
        }
        // 销毁hint窗口
        if (g_hintWindow) {
            DestroyWindow(g_hintWindow);
        }
        // 销毁指示器窗口
        if (g_indicatorWindow) {
            DestroyWindow(g_indicatorWindow);
        }
        // 销毁帮助窗口
        if (g_helpWindow) {
            DestroyWindow(g_helpWindow);
        }
        // 销毁所有标签窗口
        for (auto& tag : g_tags) {
            if (tag.hwnd) {
                DestroyWindow(tag.hwnd);
            }
        }
        g_tags.clear();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // CLI 模式检测：-c "command" 或 -f script.txt
    if (lpCmdLine && lpCmdLine[0] != '\0') {
        std::string args(lpCmdLine);

        // 分配控制台用于 CLI 输出
        AttachConsole(ATTACH_PARENT_PROCESS);
        FILE* fout = nullptr;
        FILE* ferr = nullptr;
        freopen_s(&fout, "CONOUT$", "w", stdout);
        freopen_s(&ferr, "CONOUT$", "w", stderr);

        if (args.substr(0, 2) == "-c") {
            std::string cmd = args.substr(2);
            // 去掉前导空格和引号
            size_t start = cmd.find_first_not_of(" \t\"");
            if (start != std::string::npos) cmd = cmd.substr(start);
            size_t end = cmd.find_last_not_of(" \t\"");
            if (end != std::string::npos) cmd = cmd.substr(0, end + 1);
            return RunCLIClient(cmd);
        }
        if (args.substr(0, 2) == "-f") {
            std::string path = args.substr(2);
            size_t start = path.find_first_not_of(" \t\"");
            if (start != std::string::npos) path = path.substr(start);
            size_t end = path.find_last_not_of(" \t\"");
            if (end != std::string::npos) path = path.substr(0, end + 1);
            return RunCLIScript(path);
        }
    }

    // 检查是否已经有一个实例在运行
    HANDLE hMutex = CreateMutex(NULL, TRUE, L"MouseControllerMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBox(NULL,
            IsSystemChinese() ? L"\u7A0B\u5E8F\u5DF2\u7ECF\u5728\u8FD0\u884C\u4E2D\uFF01" : L"Vimouse is already running!",
            L"Vimouse", MB_OK | MB_ICONINFORMATION);
        CloseHandle(hMutex);
        return 1;
    }

    // 注册窗口类
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MouseControllerClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, L"注册窗口类失败", L"错误", MB_OK | MB_ICONERROR);
        CloseHandle(hMutex);
        return 1;
    }

    // 创建隐藏的消息窗口（托盘模式，不在任务栏显示）
    g_hwnd = CreateWindowEx(
        0,
        L"MouseControllerClass",
        L"Vimouse",
        WS_OVERLAPPEDWINDOW,
        0, 0, 0, 0,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hwnd) {
        MessageBox(NULL, L"创建窗口失败", L"错误", MB_OK | MB_ICONERROR);
        CloseHandle(hMutex);
        return 1;
    }

    // 不显示窗口，仅通过系统托盘交互

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (g_exitRequested) break;  // 检查退出标志
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 释放互斥锁
    CloseHandle(hMutex);

    UpdateIndicatorPosition();
    return (int)msg.wParam;
}



