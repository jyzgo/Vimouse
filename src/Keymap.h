// Keymap.h - 可配置快捷键（keymap.ini）
#pragma once
#include <windows.h>
#include <string>

enum class Action {
    Toggle, ToggleCenter, ToggleRemote,
    MoveLeft, MoveDown, MoveUp, MoveRight,
    MoveUpLeft, MoveUpRight, MoveDownLeft, MoveDownRight,
    ClickLeft, ClickRight, ClickMiddle, DragToggle, ClickAndTag,
    Hint, Grid, ScreenCenter, WheelMode,
    TagPut, TagJump, TagPeek,
    HistPrev, HistNext,
    GridBack,
    Count
};

struct KeyChord {
    WORD vk = 0;
    bool ctrl = false, alt = false, shift = false;
    bool operator==(const KeyChord& o) const { return vk == o.vk && ctrl == o.ctrl && alt == o.alt && shift == o.shift; }
};

struct Modifiers { bool ctrl, alt, shift; };

extern KeyChord g_keymap[(int)Action::Count];

// 当前按键是否触发该动作。chord 未要求 shift 时忽略 shift（Shift+移动=精确、Shift+F=Shift点击）
bool IsAction(Action a, WORD vk, Modifiers m);
// 是否是任一移动键（返回 MoveDir 位，否则 0）
unsigned MoveBitOf(WORD vk, Modifiers m);
// 仅按 vk 匹配（key up 时修饰键可能已变化）
unsigned MoveBitOfVk(WORD vk);

const char*    ActionId(Action a);                 // ini 键名，如 "move_left"
const wchar_t* ActionLabel(Action a, bool zh);     // 设置界面显示名
std::wstring   VkName(WORD vk);                    // "H" / "Enter" / "." ...
std::wstring   ChordToString(const KeyChord& c);   // "Ctrl+Alt+J"
bool           ParseChord(const std::wstring& s, KeyChord& out);

void ResetKeymap();
void LoadKeymap();
void SaveKeymap();
