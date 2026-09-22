#include "Input.h"
#include "State.h"
#include "Remote.h"
#include <thread>
#include <chrono>

static DWORD DownFlag(Button b) {
    switch (b) { case Button::Left: return MOUSEEVENTF_LEFTDOWN; case Button::Right: return MOUSEEVENTF_RIGHTDOWN; default: return MOUSEEVENTF_MIDDLEDOWN; }
}
static DWORD UpFlag(Button b) {
    switch (b) { case Button::Left: return MOUSEEVENTF_LEFTUP; case Button::Right: return MOUSEEVENTF_RIGHTUP; default: return MOUSEEVENTF_MIDDLEUP; }
}
static const char* Name(Button b) {
    switch (b) { case Button::Left: return "left"; case Button::Right: return "right"; default: return "middle"; }
}

void MouseDown(Button b) {
    if (g_remoteMode) SendRemoteCmd(std::string("mousedown ") + Name(b));
    else mouse_event(DownFlag(b), 0, 0, 0, 0);
}

void MouseUp(Button b) {
    if (g_remoteMode) SendRemoteCmd(std::string("mouseup ") + Name(b));
    else mouse_event(UpFlag(b), 0, 0, 0, 0);
}

void Click(Button b) {
    if (g_remoteMode) {
        SendRemoteCmd(b == Button::Left ? "click" : b == Button::Right ? "rclick" : "mclick");
        return;
    }
    mouse_event(DownFlag(b), 0, 0, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    mouse_event(UpFlag(b), 0, 0, 0, 0);
}

void Scroll(unsigned moveBit) {
    if (g_remoteMode) {
        const char* cmd = (moveBit == MV_UP) ? "scroll up" : (moveBit == MV_DOWN) ? "scroll down"
                        : (moveBit == MV_LEFT) ? "scroll left" : (moveBit == MV_RIGHT) ? "scroll right" : nullptr;
        if (cmd) SendRemoteCmd(cmd);
        return;
    }
    switch (moveBit) {
    case MV_UP:    mouse_event(MOUSEEVENTF_WHEEL,  0, 0, (DWORD)g_wheelSpeed, 0); break;
    case MV_DOWN:  mouse_event(MOUSEEVENTF_WHEEL,  0, 0, (DWORD)(-g_wheelSpeed), 0); break;
    case MV_LEFT:  mouse_event(MOUSEEVENTF_HWHEEL, 0, 0, (DWORD)(-g_wheelSpeed), 0); break;
    case MV_RIGHT: mouse_event(MOUSEEVENTF_HWHEEL, 0, 0, (DWORD)g_wheelSpeed, 0); break;
    }
}

void ToggleDrag() {
    if (!g_isDragging) {
        GetCursorPos(&g_lastMousePos);
        MouseDown(Button::Left);
        g_isDragging = true;
    } else {
        MouseUp(Button::Left);
        g_isDragging = false;
    }
}
