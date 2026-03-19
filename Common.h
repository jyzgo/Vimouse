#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <sstream>

// 标签结构体
struct TagInfo {
    HWND hwnd;
    POINT pos;
    char letter;
    bool active;
};

// 管道命令结果
struct CommandResult {
    bool success;
    std::string message;
};

// 全局变量 - extern 声明（定义在 Vimouse.cpp）
extern HHOOK g_keyboardHook;
extern bool g_isActive;
extern int g_mouseSpeed;
extern int g_wheelSpeed;
extern bool g_isDragging;
extern POINT g_lastMousePos;
extern int g_currentScreenIndex;
extern std::vector<RECT> g_screenRects;
extern bool g_firstCPress;
extern bool g_hintMode;
extern bool g_wheelMode;
extern bool g_gridMode;
extern std::vector<RECT> g_gridStack;
extern HWND g_hintWindow;
extern std::string g_currentHint;
extern int g_hintScreenIndex;
extern bool g_lastActionWasC;
extern HWND g_indicatorWindow;
extern bool g_leftButtonDown;
extern bool g_rightButtonDown;
extern HWND g_gridWindow;
extern HWND g_hwnd;
extern bool g_exitRequested;
extern int g_dotSize;
extern bool g_iskeyDown;

extern bool g_ctrlPressed;
extern bool g_altPressed;
extern bool g_winPressed;

extern bool g_hPressed;
extern bool g_jPressed;
extern bool g_kPressed;
extern bool g_lPressed;
extern bool g_uPressed;
extern bool g_oPressed;
extern bool g_nPressed;
extern bool g_dotPressed;
extern bool g_shouldMove;

extern int g_originalSpeed;
extern bool g_isAccelerating;
extern int g_acceleratedSpeed;
extern int g_maxSpeed;
extern int g_lastSetSpeed;

extern std::vector<POINT> g_positionStack;
extern int g_mousePosIndex;
extern const int MAX_POSITIONS;

extern std::vector<TagInfo> g_tags;
extern char g_nextLetter;
extern bool g_tagMode;
extern bool g_editTagMode;
extern int g_tagTabIndex;

extern POINT g_lastMousePoint;

// 自定义消息 - 用于 Pipe Server 向主线程发送命令
#define WM_PIPE_COMMAND (WM_USER + 100)

// Pipe 名称
#define VIMOUSE_PIPE_NAME L"\\\\.\\pipe\\vimouse"

// Vimouse.cpp 中需要暴露的函数
void UpdateIndicatorPosition();
void AddMousePositionToStack();
void SaveTagsToConfig();
void ExitHintMode(bool isShowSubGrid);
void ExitWheelMode();
void ExitGridMode();
void ExitTagMode();
void EnterHintMode();
void EnterGridMode();
void EnterTagMode();
bool JumpToTag(char letter);
int GetCurrentScreenIndex();
void PutTag(POINT currentPos);
void DebugLog(const char* format, ...);
