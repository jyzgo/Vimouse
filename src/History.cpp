#include "History.h"
#include "State.h"

static long DistSq(POINT a, POINT b) { long dx = a.x - b.x, dy = a.y - b.y; return dx * dx + dy * dy; }

void AddMousePositionToStack() {
    POINT cur;
    GetCursorPos(&cur);
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
