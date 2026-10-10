#include "Hook.h"
#include "State.h"
#include "Keymap.h"
#include "Config.h"
#include "Cursor.h"
#include "Remote.h"
#include "Mover.h"
#include "Input.h"
#include "History.h"
#include "PointPeek.h"
#include "Tags.h"
#include "Hint.h"
#include "Grid.h"
#include "Clickables.h"
#include "Indicator.h"
#include "KeyOsd.h"
#include "Screens.h"
#include "Util.h"

namespace {

constexpr LRESULT kSwallow = 1;

unsigned g_arrowHeld = 0;   // 方向键模式下当前已注入"按下"的方向（MoveDir 位）

WORD ArrowVkOf(unsigned bit) {
    switch (bit) {
    case MV_LEFT: return VK_LEFT; case MV_DOWN: return VK_DOWN;
    case MV_UP: return VK_UP;     case MV_RIGHT: return VK_RIGHT;
    }
    return 0;
}

void SendArrow(WORD vk, bool up) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    in.ki.dwFlags = KEYEVENTF_EXTENDEDKEY | (up ? KEYEVENTF_KEYUP : 0);
    SendInput(1, &in, sizeof(INPUT));
}

// 方向键模式下的直线移动键 → 对应方向位（对角键不映射）
unsigned ArrowBitOf(DWORD vk) {
    unsigned bits = MoveBitOfVk((WORD)vk) & (MV_LEFT | MV_DOWN | MV_UP | MV_RIGHT);
    for (unsigned b : { MV_LEFT, MV_DOWN, MV_UP, MV_RIGHT }) if (bits & b) return b;   // 同一键绑多个方向时取第一个
    return 0;
}

bool IsModifierVk(DWORD vk) {
    switch (vk) {
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_LWIN: case VK_RWIN:
        return true;
    }
    return false;
}

bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

Modifiers CurrentMods() { return { Down(VK_CONTROL), Down(VK_MENU), Down(VK_SHIFT) }; }

// 带 Ctrl/Alt 的组合键是否被 keymap 绑定（IsAction 要求 ctrl/alt 完全一致，所以 Alt+M 不会命中 m=Hint）
bool IsBoundChord(DWORD vk, Modifiers m) {
    for (int i = 0; i < (int)Action::Count; i++)
        if (IsAction((Action)i, (WORD)vk, m)) return true;
    return false;
}

void ExitAllModes() {
    ExitHintMode(false);
    ExitGridMode();
    ExitClickMode();
    g_wheelMode = false;
    ExitTagMode();
}

void ReleaseHeldButtons() {
    if (g_leftButtonDown) { MouseUp(Button::Left); g_leftButtonDown = false; }
    if (g_isDragging)     { MouseUp(Button::Left); g_isDragging = false; }
}

void SetRemoteKeyLabel(DWORD vk) {
    if (!g_remoteMode) return;
    char c = (vk >= 'A' && vk <= 'Z') ? (char)vk : (vk == VK_OEM_PERIOD) ? '.' : '?';
    g_remoteLastKey[0] = c; g_remoteLastKey[1] = 0;
    RefreshIndicator();
}

// ---- 各模式下的按下处理。返回 true = 吞掉按键 ----

bool HandleGridKeyDown(DWORD vk, Modifiers m) {
    if (vk == VK_ESCAPE) { ExitGridMode(); return true; }
    // 方向键 ←↑→↓ 与移动键（h/j/k/l 等）等价：选对应半区
    unsigned arrowBit = (vk == VK_LEFT) ? MV_LEFT : (vk == VK_RIGHT) ? MV_RIGHT :
                        (vk == VK_UP) ? MV_UP : (vk == VK_DOWN) ? MV_DOWN : 0;
    if (unsigned bit = arrowBit ? arrowBit : MoveBitOf((WORD)vk, m)) {
        GridSelect(bit);
        if (g_miniGridMode) ExitGridMode();
        return true;
    }
    if (IsAction(Action::GridBack, (WORD)vk, m)) { GridBack(); return true; }
    if (IsAction(Action::Grid, (WORD)vk, m)) { ExitGridMode(); EnterGridMode(); return true; }   // grid 中再按：切到屏幕中心 grid
    if (IsAction(Action::ClickLeft, (WORD)vk, m)) {
        ExitGridMode();
        if (!g_leftButtonDown) { g_leftButtonDown = true; MouseDown(Button::Left); TriggerClickFlash(false); AddMousePositionToStack(); }
        return true;
    }
    if (IsAction(Action::ClickRight, (WORD)vk, m)) { ExitGridMode(); Click(Button::Right); TriggerClickFlash(true); return true; }
    if (IsAction(Action::ClickAndTag, (WORD)vk, m)) {
        ExitGridMode();
        Click(Button::Left);
        POINT p; GetCursorPos(&p);
        PutTag(p);
        return true;
    }
    if (IsAction(Action::DragToggle, (WORD)vk, m)) { ExitGridMode(); ToggleDrag(); UpdateIndicatorPosition(); return true; }
    if (IsModifierVk(vk)) return false;
    ExitGridMode();   // 其他键：退出 grid，并吞掉
    return true;
}

bool HandleHintKeyDown(DWORD vk, Modifiers m) {
    if (vk == VK_ESCAPE) { ExitHintMode(false); return true; }
    if (vk >= 'A' && vk <= 'Z' && !m.ctrl && !m.alt) { HintTypeLetter((char)vk); return true; }
    return !IsModifierVk(vk);   // 其他键全部吞掉
}

bool HandleWheelKeyDown(DWORD vk, Modifiers m) {
    if (unsigned bit = MoveBitOf((WORD)vk, m)) {
        if (bit == MV_UP || bit == MV_DOWN || bit == MV_LEFT || bit == MV_RIGHT) { Scroll(bit); return true; }
    }
    if (IsAction(Action::WheelMode, (WORD)vk, m)) { g_wheelMode = false; UpdateIndicatorPosition(); return true; }
    if (IsAction(Action::Hint, (WORD)vk, m)) { g_wheelMode = false; EnterHintMode(); return true; }
    if (IsAction(Action::ClickLeft, (WORD)vk, m)) { Click(Button::Left); TriggerClickFlash(true); AddMousePositionToStack(); return true; }
    if (IsAction(Action::ClickRight, (WORD)vk, m)) { Click(Button::Right); TriggerClickFlash(true); return true; }
    if (IsAction(Action::DragToggle, (WORD)vk, m)) { ToggleDrag(); return true; }
    if (vk == VK_ESCAPE) { g_wheelMode = false; UpdateIndicatorPosition(); return true; }
    // 其他键退出滚轮模式并放行给系统
    g_wheelMode = false;
    UpdateIndicatorPosition();
    return false;
}

bool HandleTagJumpKeyDown(DWORD vk, Modifiers m) {
    if (m.ctrl || m.alt) return false;
    if (IsAction(Action::TagJump, (WORD)vk, m)) { ExitTagMode(); return true; }
    if (vk == VK_ESCAPE) { ExitTagMode(); return true; }
    if (vk >= 'A' && vk <= 'Z') {
        if (!JumpToTag((char)vk)) ExitTagMode();
        return true;
    }
    return false;
}

// ---- 长按 a/s 预览输入点：按下显示编号圆圈，松开跳到 0 号；按住时按数字 0-4 直接跳到对应点 ----
static WORD s_peekVk = 0;
static bool s_peekOlder = true;
static bool s_peekUsed = false;

static void StartPeek(WORD vk, bool older) {
    if (s_peekVk) return;                      // 长按的自动重复
    s_peekVk = vk; s_peekOlder = older; s_peekUsed = false;
    POINT pts[PEEK_MAX];
    int n = PeekInputPoints(older, pts, PEEK_MAX);
    PointPeek_Show(pts, n);
}

bool HandleNormalKeyDown(DWORD vk, Modifiers m) {
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return false;

    SetRemoteKeyLabel(vk);

    if (unsigned bit = MoveBitOf((WORD)vk, m)) {
        g_moveKeys |= bit;
        g_lastActionWasC = false;
        StartSmoothMove();
        return true;
    }
    if (IsAction(Action::ClickLeft, (WORD)vk, m)) {
        if (!g_leftButtonDown) {
            g_leftButtonDown = true;
            if (!g_remoteMode && m.shift) keybd_event(VK_SHIFT, 0, 0, 0);
            MouseDown(Button::Left);
            if (!g_remoteMode && m.shift) keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
            TriggerClickFlash(false);
            AddMousePositionToStack();
            g_lastActionWasC = false;
        }
        return true;
    }
    if (IsAction(Action::ClickRight, (WORD)vk, m))  { Click(Button::Right);  TriggerClickFlash(true); g_lastActionWasC = false; return true; }
    if (IsAction(Action::ClickMiddle, (WORD)vk, m)) { Click(Button::Middle); TriggerClickFlash(true); g_lastActionWasC = false; return true; }
    if (IsAction(Action::DragToggle, (WORD)vk, m))  { ToggleDrag(); UpdateIndicatorPosition(); g_lastActionWasC = false; return true; }
    if (IsAction(Action::ClickAndTag, (WORD)vk, m)) {
        Click(Button::Left);
        AddMousePositionToStack();
        POINT p; GetCursorPos(&p);
        PutTag(p);
        g_lastActionWasC = false;
        return true;
    }
    if (IsAction(Action::WheelMode, (WORD)vk, m)) { g_wheelMode = true; g_lastActionWasC = false; return true; }
    if (IsAction(Action::Grid, (WORD)vk, m)) { EnterGridModeAtCursor(); g_lastActionWasC = false; return true; }
    if (IsAction(Action::Hint, (WORD)vk, m)) { EnterHintMode(); g_lastActionWasC = false; return true; }
    if (IsAction(Action::ClickMode, (WORD)vk, m)) { EnterClickMode(); g_lastActionWasC = false; return true; }
    if (IsAction(Action::ScreenCenter, (WORD)vk, m)) {
        const bool switching = g_lastActionWasC;
        if (switching) g_currentScreenIndex = (g_currentScreenIndex + 1) % (int)g_screenRects.size();   // 连按：切屏
        else g_currentScreenIndex = GetCurrentScreenIndex();
        POINT c = RectCenter(ScreenRectAt(g_currentScreenIndex));
        SetCursorPos(c.x, c.y);
        g_lastActionWasC = true;
        UpdateIndicatorPosition();
        if (switching) KeyOsd_CursorPulse(true);   // 切到另一块屏：在新位置放一次跟开启时一样的脉冲，方便找到光标
        return true;
    }
    if (IsAction(Action::TagPut, (WORD)vk, m)) { POINT p; GetCursorPos(&p); PutTag(p); return true; }
    if (IsAction(Action::TagJump, (WORD)vk, m)) { EnterTagMode(); g_lastActionWasC = false; return true; }
    if (IsAction(Action::TagPeek, (WORD)vk, m)) { EnterTagMode(); return true; }
    if (IsAction(Action::HistPrev, (WORD)vk, m)) { GoToPreviousPosition(); g_lastActionWasC = false; UpdateIndicatorPosition(); return true; }
    if (IsAction(Action::HistNext, (WORD)vk, m)) { GoToNextPosition(); g_lastActionWasC = false; UpdateIndicatorPosition(); return true; }
    if (s_peekVk && vk >= '0' && vk < '0' + PEEK_MAX) {
        PointPeek_Hide();
        JumpToInputPoint(s_peekOlder, (int)(vk - '0') + 1);
        s_peekUsed = true;
        g_lastActionWasC = false; UpdateIndicatorPosition(); return true;
    }
    if (IsAction(Action::JumpInputPointKey, (WORD)vk, m)) {    // a = 倒退（更早）
        if (!g_remoteMode) StartPeek((WORD)vk, true);
        g_lastActionWasC = false; return true;
    }
    if (IsAction(Action::JumpInputPointNext, (WORD)vk, m)) {   // s = 前进（更近）
        if (!g_remoteMode) StartPeek((WORD)vk, false);
        g_lastActionWasC = false; return true;
    }

    // 功能键 / 方向键 / 修饰键 / 数字：放行
    if (IsModifierVk(vk) || (vk >= VK_F1 && vk <= VK_F24) || (vk >= VK_LEFT && vk <= VK_DOWN) ||
        (vk >= '0' && vk <= '9') || vk == VK_TAB || vk == VK_SPACE || vk == VK_DELETE ||
        vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT || vk == VK_SNAPSHOT || vk == VK_INSERT)
        return false;

    // 激活状态下其余未绑定按键（字母、标点等）：吞掉，避免误输入
    g_lastActionWasC = false;
    UpdateIndicatorPosition();
    return true;
}

bool HandleKeyUp(DWORD vk) {
    if (s_peekVk && (WORD)vk == s_peekVk) {
        PointPeek_Hide();
        s_peekVk = 0;
        if (!s_peekUsed && g_isActive) { JumpToInputPoint(s_peekOlder); UpdateIndicatorPosition(); }
        return true;
    }
    if (unsigned bits = MoveBitOfVk((WORD)vk)) {
        g_moveKeys &= ~bits;
        if (g_moveKeys == 0) StopSmoothMove();
        return true;
    }
    if ((WORD)vk == g_keymap[(int)Action::ClickLeft].vk && g_leftButtonDown) {
        g_leftButtonDown = false;
        MouseUp(Button::Left);
        EndClickFlash();
        return true;
    }
    if ((WORD)vk == g_keymap[(int)Action::TagPeek].vk && g_tagMode) { ExitTagMode(); return true; }
    if (g_remoteMode && g_remoteLastKey[0]) { g_remoteLastKey[0] = 0; RefreshIndicator(); }
    return false;
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION) return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);

    const KBDLLHOOKSTRUCT* kb = (const KBDLLHOOKSTRUCT*)lParam;
    const DWORD vk = kb->vkCode;
    const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    const bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

    if (vk == VK_PACKET) return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);   // Unicode 注入（管道 type 命令）直接放行
    // 方向键模式：自己注入的方向键必须放行，避免被再次处理
    if (g_arrowMode && (kb->flags & LLKHF_INJECTED)) return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);

    Modifiers m = CurrentMods();
    if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) g_shiftPressed = isDown;

    // 按键 OSD：激活状态下显示；未激活时只显示开关快捷键本身
    bool isToggleChord = IsAction(Action::Toggle, (WORD)vk, m) || IsAction(Action::ToggleCenter, (WORD)vk, m) ||
                         IsAction(Action::ToggleRemote, (WORD)vk, m) || IsAction(Action::ToggleArrow, (WORD)vk, m);
    if (isDown && (g_isActive || g_arrowMode || isToggleChord)) KeyOsd_KeyDown((WORD)vk, m);
    else if (isUp) KeyOsd_KeyUp((WORD)vk);

    if (IsModifierVk(vk) || Down(VK_LWIN) || Down(VK_RWIN))
        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);

    bool swallow = false;
    if (!g_remoteMode && IsAction(Action::JumpInputPoint, (WORD)vk, m)) {
        // 全局可用（激活与否都行）：跟 a 一样逐个往前翻输入点，带 Shift 往回
        if (isDown) { JumpToInputPoint(!m.shift); UpdateIndicatorPosition(); }
        swallow = true;
    } else if ((g_isActive || g_arrowMode) && g_settings.modeHint &&
        IsAction(Action::HideModeHint, (WORD)vk, m)) {
        // 开关：按一下隐藏，再按一下显示（只在激活/方向键模式下吞掉，其余时候放行给其它程序）
        if (isDown) g_modeHintSessionOff = !g_modeHintSessionOff;
        swallow = true;
    } else if (isDown && IsAction(Action::ToggleArrow, (WORD)vk, m)) {
        SetArrowMode(!g_arrowMode);
        swallow = true;
    } else if (g_arrowMode && (isDown || isUp) && ArrowBitOf(vk) &&
               !IsAction(Action::Toggle, (WORD)vk, m) && !IsAction(Action::ToggleCenter, (WORD)vk, m) &&
               !IsAction(Action::ToggleRemote, (WORD)vk, m)) {
        // 移动键 → 方向键。修饰键保持物理状态，所以 Shift+h = Shift+←（选择），Ctrl+h = Ctrl+←
        unsigned bit = ArrowBitOf(vk);
        if (isDown) { SendArrow(ArrowVkOf(bit), false); g_arrowHeld |= bit; }
        else if (g_arrowHeld & bit) { SendArrow(ArrowVkOf(bit), true); g_arrowHeld &= ~bit; }   // 开关键残留的 key up 不发
        swallow = true;
    } else if (isDown) {
        if (IsAction(Action::ToggleRemote, (WORD)vk, m))      { ToggleRemote(); swallow = true; }
        else if (IsAction(Action::ToggleCenter, (WORD)vk, m)) { ToggleActive(true); swallow = true; }
        else if (IsAction(Action::Toggle, (WORD)vk, m))       { ToggleActive(false); swallow = true; }
        else if (!g_isActive)                                 { swallow = false; }
        // Ctrl/Alt 组合键（复制粘贴、pty_share 的 Alt+M/Alt+G 等）一律放行给前台程序，
        // 除非 keymap 明确绑定了这个组合。各子模式（hint/grid/click）同样适用
        else if ((m.ctrl || m.alt) && !IsBoundChord(vk, m))   { swallow = false; }
        else if (vk == VK_RETURN) {
            // Enter：点击并退出（Ctrl+Enter 放行）
            if (m.ctrl) swallow = false;
            else {
                ExitAllModes();
                HideAllTagWindows();
                SetActive(false);
                AddMousePositionToStack();
                Click(Button::Left);
                swallow = true;
            }
        }
        else if (g_gridMode)      swallow = HandleGridKeyDown(vk, m);
        else if (g_hintMode)      swallow = HandleHintKeyDown(vk, m);
        else if (g_clickMode)     swallow = HandleClickKeyDown(vk, m);
        else if (g_tagMode && HandleTagJumpKeyDown(vk, m)) swallow = true;
        else if (vk == VK_ESCAPE) {
            if (g_wheelMode) { g_wheelMode = false; UpdateIndicatorPosition(); }
            else if (g_tagMode) ExitTagMode();
            else SetActive(false);
            swallow = true;
        }
        else if (g_wheelMode)     swallow = HandleWheelKeyDown(vk, m);
        else                      swallow = HandleNormalKeyDown(vk, m);
    } else if (isUp) {
        swallow = HandleKeyUp(vk);
        if (!g_isActive) swallow = false;
    }

    return swallow ? kSwallow : CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
}

}  // namespace

bool InstallKeyboardHook() {
    g_keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    return g_keyboardHook != NULL;
}

void UninstallKeyboardHook() {
    if (g_keyboardHook) { UnhookWindowsHookEx(g_keyboardHook); g_keyboardHook = NULL; }
}

void SetActive(bool active) {
    if (g_isActive == active) { UpdateIndicatorPosition(); return; }
    g_isActive = active;
    if (active) {
        if (g_arrowMode) SetArrowMode(false);   // 两种模式互斥
        RefreshScreens();
        SetVimouseCursor();
        ShowTagWindowsNonInteractive();
        g_currentScreenIndex = GetCurrentScreenIndex();
        g_lastActionWasC = false;
        g_leftButtonDown = false;
        g_wheelMode = false;
    } else {
        ReleaseHeldButtons();
        StopSmoothMove();
        g_moveKeys = 0;
        ExitAllModes();
        HideAllTagWindows();
        RestoreSystemCursor();
    }
    UpdateIndicatorPosition();
}

void SetArrowMode(bool on) {
    if (on && g_isActive) SetActive(false);   // 进入方向键模式前退出鼠标模式
    if (!on) {
        // 释放仍处于按下状态的方向键，避免卡键
        for (unsigned b : { MV_LEFT, MV_DOWN, MV_UP, MV_RIGHT })
            if (g_arrowHeld & b) SendArrow(ArrowVkOf(b), true);
        g_arrowHeld = 0;
    }
    g_arrowMode = on;
    UpdateIndicatorPosition();
}

void ToggleActive(bool centerCursor) {
    SetActive(!g_isActive);
    if (g_isActive && centerCursor) {
        POINT c = RectCenter(ScreenRectAt(GetCurrentScreenIndex()));
        SetCursorPos(c.x, c.y);
        UpdateIndicatorPosition();
    }
    // 开关反馈：底部按键提示的位置显示状态 + 光标处十字准星脉冲（不管右侧提示框开没开）
    const bool zh = IsSystemChinese();
    KeyOsd_ShowStatus(g_isActive ? (zh ? L"Vimouse 已开启" : L"Vimouse ON") : (zh ? L"Vimouse 已关闭" : L"Vimouse OFF"));
    KeyOsd_CursorPulse(g_isActive);
}

void ToggleRemote() {
    if (g_remoteMode) {
        StopRemoteMode();
    } else {
        LoadRemoteHosts();
        if (g_remoteHosts.empty()) return;
        if (StartRemoteMode(g_remoteHosts[0].host, g_remoteHosts[0].exePath)) SetActive(true);
    }
    UpdateIndicatorPosition();
}
