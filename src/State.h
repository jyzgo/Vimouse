// State.h - 全局运行时状态（定义在 State.cpp）
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <atomic>

// 屏幕标签
struct TagInfo {
    HWND  hwnd;
    POINT pos;
    char  letter;
    bool  active;
};

// 平滑移动方向位
enum MoveDir : unsigned {
    MV_LEFT      = 1 << 0,
    MV_DOWN      = 1 << 1,
    MV_UP        = 1 << 2,
    MV_RIGHT     = 1 << 3,
    MV_UPLEFT    = 1 << 4,
    MV_UPRIGHT   = 1 << 5,
    MV_DOWNLEFT  = 1 << 6,
    MV_DOWNRIGHT = 1 << 7,
};

// 用户可持久化设置（settings.ini）
struct Settings {
    bool keyOsd       = false;  // 屏幕底部按键提示
    bool customCursor = true;   // 激活时使用十字准星光标
};

// 主窗口自定义消息
#define WM_APP_SET_ACTIVE (WM_APP + 2)   // wParam: 1=激活 0=停用（Pipe 线程 → 主线程）

// ---- 激活 / 模式 ----
extern HHOOK g_keyboardHook;
extern HWND  g_hwnd;                // 隐藏主窗口
extern bool  g_isActive;
extern bool  g_hintMode;
extern bool  g_gridMode;
extern bool  g_miniGridMode;        // hint 之后的单层微调 grid
extern bool  g_wheelMode;
extern bool  g_tagMode;

// ---- 修饰键（移动线程读取 shift）----
extern std::atomic<bool> g_shiftPressed;

// ---- 移动 ----
extern std::atomic<unsigned> g_moveKeys;   // MoveDir 位掩码
extern std::atomic<bool>     g_shouldMove;
extern int g_mouseSpeed;
extern int g_lastSetSpeed;
extern int g_acceleratedSpeed;
extern int g_maxSpeed;
extern int g_wheelSpeed;

// ---- 鼠标按键 ----
extern bool  g_leftButtonDown;
extern bool  g_isDragging;
extern POINT g_lastMousePos;

// ---- 屏幕 ----
extern std::vector<RECT> g_screenRects;
extern int  g_currentScreenIndex;
extern bool g_lastActionWasC;

// ---- Hint ----
extern HWND        g_hintWindow;
extern std::string g_currentHint;
extern int         g_hintScreenIndex;

// ---- Grid ----
extern HWND              g_gridWindow;
extern std::vector<RECT> g_gridStack;
extern bool              g_gridCustomCenter;
extern POINT             g_gridCenter;

// ---- 坐标指示器 ----
extern HWND g_indicatorWindow;
extern bool g_clickFlash;

// ---- 帮助悬浮窗 ----
extern HWND g_helpWindow;
extern bool g_helpVisible;

// ---- 标签 ----
extern std::vector<TagInfo> g_tags;

// ---- 位置历史 ----
extern std::vector<POINT> g_positionStack;
extern int g_mousePosIndex;
constexpr int MAX_POSITIONS = 10;

// ---- 远程 ----
extern bool        g_remoteMode;
extern std::string g_remoteHost;
extern char        g_remoteLastKey[8];

// ---- 设置 ----
extern Settings g_settings;

void DebugLog(const char* format, ...);
