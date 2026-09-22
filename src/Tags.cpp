#include "Tags.h"
#include "State.h"
#include "Config.h"
#include "Overlay.h"
#include "Keymap.h"
#include "Indicator.h"
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <algorithm>

static const int kTagSize = 30;
static HFONT g_fontSmall = NULL, g_fontBig = NULL;

static char LetterOf(HWND hwnd) {
    for (const auto& t : g_tags) if (t.hwnd == hwnd) return t.letter;
    return '?';
}

static void RemoveTagByLetter(char letter) {
    auto it = std::find_if(g_tags.begin(), g_tags.end(), [letter](const TagInfo& t) { return t.letter == letter; });
    if (it == g_tags.end()) return;
    if (it->hwnd) DestroyWindow(it->hwnd);
    g_tags.erase(it);
    SaveTags();
}

static LRESULT CALLBACK TagWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        DoubleBuffer db(hwnd);
        if (!g_fontSmall) g_fontSmall = MakeFont(16, FW_BOLD, L"Arial");
        if (!g_fontBig)   g_fontBig = MakeFont(24, FW_BOLD, L"Arial");
        FillRect(db.mem, &db.rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        HGDIOBJ old = SelectObject(db.mem, g_tagMode ? g_fontBig : g_fontSmall);
        SetTextColor(db.mem, RGB(255, 255, 255));
        char s[2] = { LetterOf(hwnd), 0 };
        DrawTextA(db.mem, s, 1, &db.rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(db.mem, old);
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (g_tagMode) { RemoveTagByLetter(LetterOf(hwnd)); return 0; }
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void ApplyInteractive(HWND hwnd, bool interactive) {
    LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    if (interactive) ex &= ~WS_EX_TRANSPARENT; else ex |= WS_EX_TRANSPARENT;
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex);
    SetLayeredWindowAttributes(hwnd, 0, interactive ? 255 : 150, LWA_ALPHA);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    InvalidateRect(hwnd, NULL, TRUE);
}

static void AddTag(char letter, int x, int y) {
    RemoveTagByLetter(letter);
    HWND hwnd = CreateOverlayWindow(L"VimouseTag", TagWndProc,
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        x - kTagSize / 2, y - kTagSize / 2, kTagSize, kTagSize, g_tagMode ? 255 : 150);
    if (!hwnd) return;
    if (g_isActive) ShowWindow(hwnd, SW_SHOWNA);
    g_tags.push_back({ hwnd, { x, y }, letter, true });
}

// 轮转分配下一个空闲字母，跳过标签命令本身占用的键
static char NextFreeLetter() {
    static int last = 0;
    WORD skip1 = g_keymap[(int)Action::TagPut].vk, skip2 = g_keymap[(int)Action::TagJump].vk;
    for (int i = 0; i < 26; i++) {
        int idx = (last + i) % 26;
        char c = (char)('A' + idx);
        if ((WORD)c == skip1 || (WORD)c == skip2) continue;
        bool used = std::any_of(g_tags.begin(), g_tags.end(), [c](const TagInfo& t) { return t.letter == c; });
        if (!used) { last = (idx + 1) % 26; return c; }
    }
    char c = (char)('A' + last);
    last = (last + 1) % 26;
    return c;
}

void PutTag(POINT p) {
    for (auto it = g_tags.begin(); it != g_tags.end(); ++it) {
        long dx = it->pos.x - p.x, dy = it->pos.y - p.y;
        if (dx * dx + dy * dy < 600) {  // ~24px 内：当作同一位置 → 移除
            if (it->hwnd) DestroyWindow(it->hwnd);
            g_tags.erase(it);
            SaveTags();
            return;
        }
    }
    AddTag(NextFreeLetter(), p.x, p.y);
    SaveTags();
}

bool JumpToTag(char letter) {
    for (const auto& t : g_tags) {
        if (t.letter != letter) continue;
        SetCursorPos(t.pos.x, t.pos.y);
        ExitTagMode();
        return true;
    }
    return false;
}

void EnterTagMode() {
    g_tagMode = true;
    for (auto& t : g_tags) if (t.hwnd) ApplyInteractive(t.hwnd, true);
}

void ExitTagMode() {
    g_tagMode = false;
    for (auto& t : g_tags) if (t.hwnd) ApplyInteractive(t.hwnd, false);
    UpdateIndicatorPosition();
}

void HideAllTagWindows() {
    for (auto& t : g_tags) if (t.hwnd) ShowWindow(t.hwnd, SW_HIDE);
}

void ShowTagWindowsNonInteractive() {
    for (auto& t : g_tags) if (t.hwnd) ApplyInteractive(t.hwnd, false);
}

void DestroyAllTags() {
    for (auto& t : g_tags) if (t.hwnd) DestroyWindow(t.hwnd);
    g_tags.clear();
    if (g_fontSmall) { DeleteObject(g_fontSmall); g_fontSmall = NULL; }
    if (g_fontBig)   { DeleteObject(g_fontBig);   g_fontBig = NULL; }
}

void SaveTags() {
    std::ofstream f(ConfigFilePath("tags.txt"));
    f << "# letter,x,y\n";
    for (const auto& t : g_tags) if (t.active) f << t.letter << "," << t.pos.x << "," << t.pos.y << "\n";
}

void LoadTags() {
    std::ifstream f(ConfigFilePath("tags.txt"));
    if (!f.is_open()) {
        // 旧版位置：%USERPROFILE%\Vimouse\.config，格式 key,letter,x,y
        char home[MAX_PATH];
        if (SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, home) == S_OK)
            f.open(std::string(home) + "\\Vimouse\\.config");
    }
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> parts;
        std::istringstream iss(line);
        std::string tok;
        while (std::getline(iss, tok, ',')) parts.push_back(tok);
        if (parts.size() == 4) parts.erase(parts.begin());   // 旧格式去掉 key
        if (parts.size() != 3 || parts[0].empty()) continue;
        try {
            AddTag((char)toupper(parts[0][0]), std::stoi(parts[1]), std::stoi(parts[2]));
        } catch (...) {}
    }
}
