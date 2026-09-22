#include "State.h"
#include <cstdarg>
#include <cstdio>

HHOOK g_keyboardHook = NULL;
HWND  g_hwnd = NULL;
bool  g_isActive = true;
bool  g_hintMode = false;
bool  g_gridMode = false;
bool  g_miniGridMode = false;
bool  g_wheelMode = false;
bool  g_tagMode = false;

std::atomic<bool> g_shiftPressed{ false };

std::atomic<unsigned> g_moveKeys{ 0 };
std::atomic<bool>     g_shouldMove{ false };
int g_mouseSpeed = 10;
int g_lastSetSpeed = 10;
int g_acceleratedSpeed = 10;
int g_maxSpeed = 2000;
int g_wheelSpeed = 300;

bool  g_leftButtonDown = false;
bool  g_isDragging = false;
POINT g_lastMousePos = { 0, 0 };

std::vector<RECT> g_screenRects;
int  g_currentScreenIndex = 0;
bool g_lastActionWasC = false;

HWND        g_hintWindow = NULL;
std::string g_currentHint;
int         g_hintScreenIndex = 0;

HWND              g_gridWindow = NULL;
std::vector<RECT> g_gridStack;
bool              g_gridCustomCenter = false;
POINT             g_gridCenter = { 0, 0 };

HWND g_indicatorWindow = NULL;
bool g_clickFlash = false;

HWND g_helpWindow = NULL;
bool g_helpVisible = false;

std::vector<TagInfo> g_tags;

std::vector<POINT> g_positionStack;
int g_mousePosIndex = -1;

bool        g_remoteMode = false;
std::string g_remoteHost;
char        g_remoteLastKey[8] = "";

Settings g_settings;

void DebugLog(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    OutputDebugStringA(buffer);
}
