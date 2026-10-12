#include "Scroller.h"
#include "State.h"
#include <windows.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace {

constexpr unsigned kAxisBits = MV_UP | MV_DOWN | MV_LEFT | MV_RIGHT;

std::atomic<unsigned> s_held{ 0 };       // 当前按住的方向（钩子线程写，滚动线程读）
std::atomic<unsigned> s_kick{ 0 };       // 新按下、滚动线程还没处理的方向
std::atomic<bool>     s_running{ false };

// 速度单位：wheel delta / 秒（120 = 一格 ≈ 3 行）
// 起始/封顶/加速/衰减来自 g_settings（设置「速度」页可调，默认 2600 / 20000 / 15000 / 9）
constexpr double kFollow     = 14;     // 速度追目标速度的快慢（1/秒）
constexpr double kStopSpeed  = 40;     // 低于这个速度直接停
constexpr int    kTickMs     = 8;

struct Axis {
    double v = 0;      // 当前速度（带符号）
    double acc = 0;    // 还没发出去的小数部分
    double held = 0;   // 按住时长（秒），决定加速到多快；松开后慢慢回落，连点能接上之前的速度
};

void Emit(bool horizontal, int delta) {
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = horizontal ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
    in.mi.mouseData = (DWORD)delta;
    SendInput(1, &in, sizeof(INPUT));
}

// dir: +1 / -1 / 0（没按，或两个相反方向同时按）
// 不做 Shift 慢速：用户按着 Shift 时注入的竖直滚轮，浏览器等会当成横向滚动
void Step(Axis& a, int dir, bool kicked, double dt, bool horizontal) {
    const double kStartSpeed = g_settings.scrollStart;
    const double kMaxSpeed   = g_settings.scrollMax;
    const double kRampPerSec = g_settings.scrollRamp;
    const double kFriction   = g_settings.scrollFriction;
    if (dir) {
        const double base = kStartSpeed;
        const bool same = a.v * dir > 0;
        if (!same) { a.v = dir * base; a.acc = 0; a.held = 0; }     // 刚按下或反向：立刻按起始速度走
        else if (kicked && std::fabs(a.v) < base) a.v = dir * base;  // 滑行中再按同方向：不低于起始速度
        a.held += dt;
        const double target = (std::min)(kMaxSpeed, kStartSpeed + kRampPerSec * a.held);
        a.v += (dir * target - a.v) * (std::min)(1.0, kFollow * dt);
    } else {
        a.held = (std::max)(0.0, a.held - dt * 3);
        a.v *= std::exp(-kFriction * dt);
        if (std::fabs(a.v) < kStopSpeed) { a.v = 0; a.acc = 0; }
    }
    a.acc += a.v * dt;
    const int out = (int)a.acc;   // 向零取整，余数留到下一帧
    if (out) { a.acc -= out; Emit(horizontal, out); }
}

int DirOf(unsigned bits, unsigned plus, unsigned minus) {
    return ((bits & plus) ? 1 : 0) - ((bits & minus) ? 1 : 0);
}

void Run() {
    // 高精度定时器（Win10 1803+）让每帧真的是 ~8ms；拿不到就退回 Sleep（约 15ms 一帧，速度按实际 dt 算，不受影响）
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    Axis vert, horz;
    auto last = std::chrono::steady_clock::now();
    for (;;) {
        if (timer) {
            LARGE_INTEGER due;
            due.QuadPart = -10000LL * kTickMs;   // 负数 = 相对时间，单位 100ns
            SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE);
            WaitForSingleObject(timer, 50);
        } else {
            Sleep(kTickMs);
        }
        const auto now = std::chrono::steady_clock::now();
        const double dt = (std::min)(0.05, std::chrono::duration<double>(now - last).count());
        last = now;

        const unsigned held = s_held.load();
        const unsigned kick = s_kick.exchange(0);
        const unsigned eff = held | kick;   // 两帧之间按下又松开的极短点按也算按了一帧
        Step(vert, DirOf(eff, MV_UP, MV_DOWN), (kick & (MV_UP | MV_DOWN)) != 0, dt, false);
        Step(horz, DirOf(eff, MV_RIGHT, MV_LEFT), (kick & (MV_LEFT | MV_RIGHT)) != 0, dt, true);

        if (!eff && vert.v == 0 && horz.v == 0) {
            s_running = false;
            // 退出前再看一眼：这期间刚好有新按键、且没有别人接手，就自己接着跑
            if ((s_held.load() || s_kick.load()) && !s_running.exchange(true)) continue;
            break;
        }
    }
    if (timer) CloseHandle(timer);
}

void EnsureThread() {
    if (s_running.exchange(true)) return;
    std::thread(Run).detach();   // 线程空闲后自己退出；detach 避免进程退出时 joinable 的 std::thread 触发 terminate
}

}  // namespace

void ScrollKeyDown(unsigned moveBit) {
    moveBit &= kAxisBits;
    if (!moveBit || (s_held.load() & moveBit)) return;   // 自动重复
    s_held |= moveBit;
    s_kick |= moveBit;
    EnsureThread();
}

void ScrollKeyUp(unsigned bits) {
    s_held &= ~(bits & kAxisBits);
}

void ScrollStopAll() {
    s_held = 0;
}
