#include "Mover.h"
#include "State.h"
#include "Cursor.h"
#include "Remote.h"
#include "Indicator.h"
#include <thread>
#include <chrono>
#include <string>

static std::thread g_moveThread;

static void MoveThread() {
    DWORD lastAccel = GetTickCount();
    while (g_shouldMove) {
        unsigned keys = g_moveKeys.load();
        if (keys) {
            POINT cur;
            GetCursorPos(&cur);
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
                    SetCursorPos(cur.x + dx, cur.y + dy);
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
