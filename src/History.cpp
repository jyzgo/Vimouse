#include "History.h"
#include "State.h"
#include <vector>

static long DistSq(POINT a, POINT b) { long dx = a.x - b.x, dy = a.y - b.y; return dx * dx + dy * dy; }

// ---- 输入点历史（a / Ctrl+Alt+O）：最多 100 个，按时间顺序，末尾最新 ----
static const int kMaxInputs = 100;
static std::vector<POINT> s_inputs;
static int   s_walk = -1;        // 正在回溯时当前停在哪个输入点；-1 = 没在回溯
static POINT s_returnPt = {};    // 开始回溯前光标所在位置（翻到头后回到这里）
static POINT s_lastJump = {};    // 上一次跳到的位置；光标被移开就算结束回溯

static void RecordInputPoint(POINT cur) {
    if (!s_inputs.empty() && DistSq(cur, s_inputs.back()) < 25) { s_walk = -1; return; }   // 5px 内视为同一点
    if ((int)s_inputs.size() >= kMaxInputs) s_inputs.erase(s_inputs.begin());
    s_inputs.push_back(cur);
    s_walk = -1;
}

void AddMousePositionToStack() {
    POINT cur;
    GetCursorPos(&cur);
    RecordInputPoint(cur);
    if (!g_positionStack.empty() && DistSq(cur, g_positionStack.back()) < 25) return;  // 5px 内视为同一位置
    if ((int)g_positionStack.size() >= MAX_POSITIONS) g_positionStack.erase(g_positionStack.begin());
    g_positionStack.push_back(cur);
    g_mousePosIndex = (int)g_positionStack.size() - 1;
}

static void GoTo(int idx) {
    if (idx < 0 || idx >= (int)g_positionStack.size()) return;
    g_mousePosIndex = idx;
    SetCursorPos(g_positionStack[idx].x, g_positionStack[idx].y);
}

void GoToPreviousPosition() {
    if (g_positionStack.empty()) return;
    GoTo(g_mousePosIndex > 0 ? g_mousePosIndex - 1 : (int)g_positionStack.size() - 1);
}

void GoToNextPosition() {
    if (g_positionStack.empty()) return;
    GoTo(g_mousePosIndex < (int)g_positionStack.size() - 1 ? g_mousePosIndex + 1 : 0);
}

// 从当前状态往某方向依次会到达的输入点下标（out[0] = 跳 1 步到的点）
static void Targets(bool older, std::vector<int>& out, bool& walking) {
    out.clear();
    const int n = (int)s_inputs.size();
    POINT cur;
    GetCursorPos(&cur);
    walking = s_walk >= 0 && s_walk < n && DistSq(cur, s_lastJump) < 25;
    if (walking) {
        if (older) for (int i = s_walk - 1; i >= 0; i--) out.push_back(i);
        else       for (int i = s_walk + 1; i < n; i++)  out.push_back(i);
    } else if (older && n > 0) {
        int start = n - 1;
        if (DistSq(cur, s_inputs[start]) < 25) start--;   // 光标已在最新点上，从上一个开始
        for (int i = start; i >= 0; i--) out.push_back(i);
    }
}

int PeekInputPoints(bool older, POINT* out, int maxN) {
    std::vector<int> t; bool walking;
    Targets(older, t, walking);
    int k = 0;
    for (; k < maxN && k < (int)t.size(); k++) out[k] = s_inputs[t[k]];
    return k;
}

void JumpToInputPoint(bool older, int steps) {
    if (steps < 1) steps = 1;
    std::vector<int> t; bool walking;
    Targets(older, t, walking);
    if (steps > (int)t.size()) {
        // 翻过头 → 回到开始回溯前的位置，结束回溯
        if (walking) { SetCursorPos(s_returnPt.x, s_returnPt.y); s_walk = -1; }
        return;
    }
    if (!walking) GetCursorPos(&s_returnPt);
    s_walk = t[steps - 1];
    s_lastJump = s_inputs[s_walk];
    SetCursorPos(s_lastJump.x, s_lastJump.y);
}
