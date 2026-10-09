#include "Mover.h"
#include "State.h"
#include "Cursor.h"
#include "Remote.h"
#include "Indicator.h"
#include <thread>
#include <chrono>
#include <string>

static std::thread g_moveThread;

// 设置里开了 input_move：用 SendInput 绝对坐标移动，而不是 SetCursorPos。
// SetCursorPos 不经过低级鼠标钩子，Deskflow 主控端看不到移动，推到屏幕边缘也不切屏；
// SendInput 的绝对移动会经过钩子，且不受系统指针加速影响（hjkl 速度/Shift 1 像素精度不变）。
static void MoveCursorTo(int x, int y) {
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (!g_settings.inputMove || vw <= 1 || vh <= 1) { SetCursorPos(x, y); return; }
    x = max(vx, min(vx + vw - 1, x));
    y = max(vy, min(vy + vh - 1, y));
    INPUT in = {};
    in.type = INPUT_MOUSE;
    // 系统按 像素 = floor(d * (size-1) / 65535) 换算（2fpc 实测），所以这里取 ceil(off * 65535 / (size-1))，
    // 每个像素都能精确落到，包括最右/最下那一列。旧写法 off*65536/size 最右只能到倒数第二列，
    // 差 1 像素碰不到 Deskflow 的 1 像素切屏区
    in.mi.dx = (LONG)(((long long)(x - vx) * 65535 + (vw - 2)) / (vw - 1));
    in.mi.dy = (LONG)(((long long)(y - vy) * 65535 + (vh - 2)) / (vh - 1));
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &in, sizeof(INPUT));
}

static void MoveThread() {
    DWORD lastAccel = GetTickCount();
    // 上一帧发出移动时的起点和目标。光标合法的落点只会在两者构成的矩形内（异步还没生效 = 起点，
    // 被屏幕边缘截住 = 中途）；落在矩形外说明有别人挪了光标 —— 真鼠标，或 Deskflow 切屏时把光标
    // 拉回屏幕中心。这时按键松开事件多半已被 Deskflow 吞掉（转发给了被控端），Vimouse 永远等不到
    // key up，会一直往边上推、把人踢回被控端。所以一旦检测到就清空移动键，停止移动。
    bool havePrev = false;
    POINT prev = {}, target = {};
    while (g_shouldMove) {
        unsigned keys = g_moveKeys.load();
        if (!keys) havePrev = false;
        if (keys) {
            POINT cur;
            GetCursorPos(&cur);
            if (havePrev && !g_remoteMode) {
                const int slack = 2;
                bool inside = cur.x >= min(prev.x, target.x) - slack && cur.x <= max(prev.x, target.x) + slack &&
                              cur.y >= min(prev.y, target.y) - slack && cur.y <= max(prev.y, target.y) + slack;
                if (!inside) {
                    g_moveKeys = 0;
                    g_mouseSpeed = g_lastSetSpeed;
                    havePrev = false;
                    continue;
                }
            }
            int dx = 0, dy = 0;

            if (g_shiftPressed) {
                // 精确模式：每帧 1 像素，不加速
                if (keys & MV_LEFT)  dx -= 1;
                if (keys & MV_RIGHT) dx += 1;
                if (keys & MV_UP)    dy -= 1;
                if (keys & MV_DOWN)  dy += 1;
                if (keys & MV_UPLEFT)    { dx -= 1; dy -= 1; }
                if (keys & MV_UPRIGHT)   { dx += 1; dy -= 1; }
                if (keys & MV_DOWNLEFT)  { dx -= 1; dy += 1; }
                if (keys & MV_DOWNRIGHT) { dx += 1; dy += 1; }
            } else {
                if (GetTickCount() - lastAccel > 5) {
                    g_mouseSpeed = min(g_mouseSpeed + g_acceleratedSpeed, g_maxSpeed);
                    lastAccel = GetTickCount();
                }
                int s = g_mouseSpeed / 10;       // 直线步长
                int d = g_mouseSpeed / 7;        // 对角步长（略大，视觉速度接近）
                if (keys & MV_LEFT)  dx -= s;
                if (keys & MV_RIGHT) dx += s;
                if (keys & MV_UP)    dy -= s;
                if (keys & MV_DOWN)  dy += s;
                if (keys & MV_UPLEFT)    { dx -= d; dy -= d; }
                if (keys & MV_UPRIGHT)   { dx += d; dy -= d; }
                if (keys & MV_DOWNLEFT)  { dx -= d; dy += d; }
                if (keys & MV_DOWNRIGHT) { dx += d; dy += d; }
            }

            if (dx || dy) {
                if (g_remoteMode) {
                    SendRemoteCmd("rmove " + std::to_string(dx) + " " + std::to_string(dy));
                } else {
                    MoveCursorTo(cur.x + dx, cur.y + dy);
                    prev = cur;
                    target = { cur.x + dx, cur.y + dy };
                    havePrev = true;
                    // 拖拽中持续给出按下事件，部分程序才会识别为拖动
                    if (g_isDragging) mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void StartSmoothMove() {
    if (g_shouldMove) return;
    g_shouldMove = true;
    SetMovingCursor();
    g_moveThread = std::thread(MoveThread);
}

void StopSmoothMove() {
    if (!g_shouldMove) return;
    g_shouldMove = false;
    if (g_moveThread.joinable()) g_moveThread.join();
    g_mouseSpeed = g_lastSetSpeed;
    if (g_isActive) SetVimouseCursor();
    UpdateIndicatorPosition();
}
