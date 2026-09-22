#include "Keymap.h"
#include "Config.h"
#include "State.h"
#include "Util.h"
#include <fstream>
#include <algorithm>
#include <cwctype>

KeyChord g_keymap[(int)Action::Count];

namespace {

struct ActionMeta { Action a; const char* id; const wchar_t* zh; const wchar_t* en; KeyChord def; };

KeyChord K(WORD vk, bool c = false, bool alt = false, bool s = false) { KeyChord k; k.vk = vk; k.ctrl = c; k.alt = alt; k.shift = s; return k; }

const ActionMeta kMeta[] = {
    { Action::Toggle,        "toggle",         L"开关 Vimouse",        L"Toggle Vimouse",         K('J', true) },
    { Action::ToggleCenter,  "toggle_center",  L"开关并居中光标",      L"Toggle + center cursor", K('K', true, true) },
    { Action::ToggleRemote,  "toggle_remote",  L"开关远程模式",        L"Toggle remote mode",     K('J', true, true) },
    { Action::MoveLeft,      "move_left",      L"左移",                L"Move left",              K('H') },
    { Action::MoveDown,      "move_down",      L"下移",                L"Move down",              K('J') },
    { Action::MoveUp,        "move_up",        L"上移",                L"Move up",                K('K') },
    { Action::MoveRight,     "move_right",     L"右移",                L"Move right",             K('L') },
    { Action::MoveUpLeft,    "move_upleft",    L"左上移",              L"Move up-left",           K('U') },
    { Action::MoveUpRight,   "move_upright",   L"右上移",              L"Move up-right",          K('O') },
    { Action::MoveDownLeft,  "move_downleft",  L"左下移",              L"Move down-left",         K('N') },
    { Action::MoveDownRight, "move_downright", L"右下移",              L"Move down-right",        K(VK_OEM_PERIOD) },
    { Action::ClickLeft,     "click_left",     L"左键(按住不松)",      L"Left click (hold)",      K('F') },
    { Action::ClickRight,    "click_right",    L"右键",                L"Right click",            K('G') },
    { Action::ClickMiddle,   "click_middle",   L"中键",                L"Middle click",           K('B') },
    { Action::DragToggle,    "drag_toggle",    L"拖拽开关",            L"Drag toggle",            K('V') },
    { Action::ClickAndTag,   "click_and_tag",  L"点击并放标签",        L"Click + place tag",      K('T') },
    { Action::Hint,          "hint",           L"Hint 坐标跳转",       L"Hint jump",              K('M') },
    { Action::Grid,          "grid",           L"Grid 二分定位",       L"Grid bisect",            K('I') },
    { Action::ScreenCenter,  "screen_center",  L"屏幕中心/切屏",       L"Screen center / switch", K('C') },
    { Action::WheelMode,     "wheel_mode",     L"滚轮模式",            L"Scroll mode",            K('Y') },
    { Action::TagPut,        "tag_put",        L"放置/移除标签",       L"Place / remove tag",     K('Q') },
    { Action::TagJump,       "tag_jump",       L"标签跳转模式",        L"Tag jump mode",          K('W') },
    { Action::TagPeek,       "tag_peek",       L"按住查看标签",        L"Hold to peek tags",      K(VK_BACK) },
    { Action::HistPrev,      "hist_prev",      L"上一个历史位置",      L"Previous position",      K('R') },
    { Action::HistNext,      "hist_next",      L"下一个历史位置",      L"Next position",          K('E') },
    { Action::GridBack,      "grid_back",      L"Grid: 返回上一级",    L"Grid: back",             K('R') },
};
static_assert(sizeof(kMeta) / sizeof(kMeta[0]) == (size_t)Action::Count, "kMeta must cover every Action");

struct NamedVk { const wchar_t* name; WORD vk; };
const NamedVk kNamedKeys[] = {
    { L"Enter", VK_RETURN }, { L"Esc", VK_ESCAPE }, { L"Backspace", VK_BACK }, { L"Tab", VK_TAB },
    { L"Space", VK_SPACE }, { L"Delete", VK_DELETE }, { L"Insert", VK_INSERT },
    { L"Home", VK_HOME }, { L"End", VK_END }, { L"PageUp", VK_PRIOR }, { L"PageDown", VK_NEXT },
    { L"Left", VK_LEFT }, { L"Right", VK_RIGHT }, { L"Up", VK_UP }, { L"Down", VK_DOWN },
    { L".", VK_OEM_PERIOD }, { L",", VK_OEM_COMMA }, { L"-", VK_OEM_MINUS }, { L"=", VK_OEM_PLUS },
    { L";", VK_OEM_1 }, { L"/", VK_OEM_2 }, { L"`", VK_OEM_3 }, { L"[", VK_OEM_4 }, { L"\\", VK_OEM_5 },
    { L"]", VK_OEM_6 }, { L"'", VK_OEM_7 },
    { L"F1", VK_F1 }, { L"F2", VK_F2 }, { L"F3", VK_F3 }, { L"F4", VK_F4 }, { L"F5", VK_F5 }, { L"F6", VK_F6 },
    { L"F7", VK_F7 }, { L"F8", VK_F8 }, { L"F9", VK_F9 }, { L"F10", VK_F10 }, { L"F11", VK_F11 }, { L"F12", VK_F12 },
    { L"Ctrl", VK_CONTROL }, { L"Alt", VK_MENU }, { L"Shift", VK_SHIFT }, { L"Win", VK_LWIN },
};

std::wstring Lower(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }

}  // namespace

const char* ActionId(Action a) { return kMeta[(int)a].id; }
const wchar_t* ActionLabel(Action a, bool zh) { return zh ? kMeta[(int)a].zh : kMeta[(int)a].en; }

std::wstring VkName(WORD vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::wstring(1, (wchar_t)vk);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return L"Num" + std::wstring(1, (wchar_t)('0' + vk - VK_NUMPAD0));
    if (vk == VK_LCONTROL || vk == VK_RCONTROL) vk = VK_CONTROL;
    if (vk == VK_LMENU || vk == VK_RMENU) vk = VK_MENU;
    if (vk == VK_LSHIFT || vk == VK_RSHIFT) vk = VK_SHIFT;
    if (vk == VK_RWIN) vk = VK_LWIN;
    for (const auto& n : kNamedKeys) if (n.vk == vk) return n.name;
    wchar_t buf[64];
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    if (sc && GetKeyNameTextW((LONG)(sc << 16), buf, 64) > 0) return buf;
    return L"VK" + std::to_wstring(vk);
}

std::wstring ChordToString(const KeyChord& c) {
    std::wstring s;
    if (c.ctrl) s += L"Ctrl+";
    if (c.alt) s += L"Alt+";
    if (c.shift) s += L"Shift+";
    return s + VkName(c.vk);
}

bool ParseChord(const std::wstring& text, KeyChord& out) {
    KeyChord k;
    std::wstring s = text;
    s.erase(std::remove_if(s.begin(), s.end(), [](wchar_t c) { return iswspace(c) != 0; }), s.end());
    if (s.empty()) return false;

    size_t pos = 0;
    std::wstring key;
    while (pos < s.size()) {
        size_t plus = s.find(L'+', pos);
        std::wstring part = (plus == std::wstring::npos) ? s.substr(pos) : s.substr(pos, plus - pos);
        if (part.empty()) return false;
        std::wstring lp = Lower(part);
        if (plus != std::wstring::npos) {
            if (lp == L"ctrl" || lp == L"control") k.ctrl = true;
            else if (lp == L"alt") k.alt = true;
            else if (lp == L"shift") k.shift = true;
            else return false;
            pos = plus + 1;
        } else {
            key = part;
            break;
        }
    }
    if (key.empty()) return false;

    std::wstring lk = Lower(key);
    if (key.size() == 1) {
        wchar_t c = (wchar_t)towupper(key[0]);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) { k.vk = (WORD)c; out = k; return true; }
    }
    for (const auto& n : kNamedKeys) {
        if (Lower(n.name) == lk) {
            if (n.vk == VK_CONTROL || n.vk == VK_MENU || n.vk == VK_SHIFT || n.vk == VK_LWIN) return false;
            k.vk = n.vk; out = k; return true;
        }
    }
    if (lk == L"period") { k.vk = VK_OEM_PERIOD; out = k; return true; }
    if (lk == L"comma")  { k.vk = VK_OEM_COMMA;  out = k; return true; }
    if (lk == L"escape") { k.vk = VK_ESCAPE;     out = k; return true; }
    if (lk == L"return") { k.vk = VK_RETURN;     out = k; return true; }
    return false;
}

bool IsAction(Action a, WORD vk, Modifiers m) {
    const KeyChord& c = g_keymap[(int)a];
    if (c.vk == 0 || c.vk != vk) return false;
    if (c.ctrl != m.ctrl || c.alt != m.alt) return false;
    if (c.shift && !m.shift) return false;
    return true;
}

static const struct { Action a; unsigned bit; } kMoveBits[] = {
    { Action::MoveLeft, MV_LEFT }, { Action::MoveDown, MV_DOWN }, { Action::MoveUp, MV_UP }, { Action::MoveRight, MV_RIGHT },
    { Action::MoveUpLeft, MV_UPLEFT }, { Action::MoveUpRight, MV_UPRIGHT },
    { Action::MoveDownLeft, MV_DOWNLEFT }, { Action::MoveDownRight, MV_DOWNRIGHT },
};

unsigned MoveBitOf(WORD vk, Modifiers m) {
    for (const auto& mb : kMoveBits) if (IsAction(mb.a, vk, m)) return mb.bit;
    return 0;
}

unsigned MoveBitOfVk(WORD vk) {
    unsigned bits = 0;
    for (const auto& mb : kMoveBits) if (g_keymap[(int)mb.a].vk == vk) bits |= mb.bit;
    return bits;
}

void ResetKeymap() {
    for (const auto& m : kMeta) g_keymap[(int)m.a] = m.def;
}

void LoadKeymap() {
    ResetKeymap();
    std::ifstream f(ConfigFilePath("keymap.ini"));
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string id = line.substr(0, eq);
        id.erase(id.find_last_not_of(" \t\r") + 1);
        KeyChord k;
        if (!ParseChord(Utf8ToWide(line.substr(eq + 1)), k)) {
            DebugLog("Keymap: bad chord for %s\n", id.c_str());
            continue;
        }
        for (const auto& m : kMeta) if (id == m.id) { g_keymap[(int)m.a] = k; break; }
    }
}

void SaveKeymap() {
    std::ofstream f(ConfigFilePath("keymap.ini"));
    f << "# Vimouse keymap. Format: action=Key, with optional Ctrl+ / Alt+ / Shift+ prefixes.\n";
    f << "# Key names: A-Z 0-9 Enter Esc Backspace Tab Space . , - = ; / [ ] Left Right Up Down F1-F12\n";
    for (const auto& m : kMeta)
        f << m.id << "=" << WideToUtf8(ChordToString(g_keymap[(int)m.a])) << "\n";
}
