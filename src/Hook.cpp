#include "Hook.h"
#include "State.h"
#include "Keymap.h"
#include "Config.h"
#include "Cursor.h"
#include "Remote.h"
#include "Mover.h"
#include "Input.h"
#include "History.h"
#include "Tags.h"
#include "Hint.h"
#include "Grid.h"
#include "Indicator.h"
#include "KeyOsd.h"
#include "Screens.h"

namespace {

constexpr LRESULT kSwallow = 1;

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

void ExitAllModes() {
    ExitHintMode(false);
    ExitGridMode();
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
    if (unsigned bit = MoveBitOf((WORD)vk, m)) {
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
    if (IsAction(Action::ClickLeft, (WORD)vk, m)) { Click(Button::Left); TriggerClickFlash(true); return true; }
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

bool HandleNormalKeyDown(DWORD vk, Modifiers m) {
    // Ctrl+字母组合一律放行（复制粘贴等），除非 keymap 明确绑定了 Ctrl 组合
    if (m.ctrl && vk >= 'A' && vk <= 'Z') {
        bool bound = false;
        for (int i = 0; i < (int)Action::Count; i++)
            if (g_keymap[i].ctrl && IsAction((Action)i, (WORD)vk, m)) { bound = true; break; }
        if (!bound) return false;
    }
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
    if (IsAction(Action::ScreenCenter, (WORD)vk, m)) {
        if (g_lastActionWasC) g_currentScreenIndex = (g_currentScreenIndex + 1) % (int)g_screenRects.size();   // 连按：切屏
        else g_currentScreenIndex = GetCurrentScreenIndex();
        POINT c = RectCenter(ScreenRectAt(g_currentScreenIndex));
        SetCursorPos(c.x, c.y);
        g_lastActionWasC = true;
        UpdateIndicatorPosition();
        return true;
    }
    if (IsAction(Action::TagPut, (WORD)vk, m)) { POINT p; GetCursorPos(&p); PutTag(p); return true; }
    if (IsAction(Action::TagJump, (WORD)vk, m)) { EnterTagMode(); g_lastActionWasC = false; return true; }
    if (IsAction(Action::TagPeek, (WORD)vk, m)) { EnterTagMode(); return true; }
    if (IsAction(Action::HistPrev, (WORD)vk, m)) { GoToPreviousPosition(); g_lastActionWasC = false; UpdateIndicatorPosition(); return true; }
    if (IsAction(Action::HistNext, (WORD)vk, m)) { GoToNextPosition(); g_lastActionWasC = false; UpdateIndicatorPosition(); return true; }

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

    Modifiers m = CurrentMods();
    if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) g_shiftPressed = isDown;

    // 按键 OSD：激活状态下显示；未激活时只显示开关快捷键本身
    bool isToggleChord = IsAction(Action::Toggle, (WORD)vk, m) || IsAction(Action::ToggleCenter, (WORD)vk, m) || IsAction(Action::ToggleRemote, (WORD)vk, m);
    if (isDown && (g_isActive || isToggleChord)) KeyOsd_KeyDown((WORD)vk, m);
    else if (isUp) KeyOsd_KeyUp((WORD)vk);

    if (IsModifierVk(vk) || Down(VK_LWIN) || Down(VK_RWIN))
        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);

    bool swallow = false;
    if (isDown) {
        if (IsAction(Action::ToggleRemote, (WORD)vk, m))      { ToggleRemote(); swallow = true; }
        else if (IsAction(Action::ToggleCenter, (WORD)vk, m)) { ToggleActive(true); swallow = true; }
        else if (IsAction(Action::Toggle, (WORD)vk, m))       { ToggleActive(false); swallow = true; }
        else if (!g_isActive)                                 { swallow = false; }
        else if (vk == VK_RETURN) {
            // Enter：点击并退出（Ctrl+Enter 放行）
            if (m.ctrl) swallow = false;
            else {
                ExitAllModes();
                HideAllTagWindows();
                SetActive(false);
                Click(Button::Left);
                swallow = true;
            }
        }
        else if (g_gridMode)      swallow = HandleGridKeyDown(vk, m);
        else if (g_hintMode)      swallow = HandleHintKeyDown(vk, m);
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

void ToggleActive(bool centerCursor) {
    SetActive(!g_isActive);
    if (g_isActive && centerCursor) {
        POINT c = RectCenter(ScreenRectAt(GetCurrentScreenIndex()));
        SetCursorPos(c.x, c.y);
        UpdateIndicatorPosition();
    }
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
