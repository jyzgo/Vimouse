#include "SettingsDialog.h"
#include "State.h"
#include "Config.h"
#include "Keymap.h"
#include "Util.h"
#include "Cursor.h"
#include "HelpOverlay.h"
#include "resource.h"
#include <commctrl.h>
#include <string>

#pragma comment(lib, "comctl32.lib")

namespace {

enum Ctl : int {
    ID_TAB = 2000,
    // 常规
    ID_AUTOSTART = 2100, ID_KEYOSD, ID_CUSTOMCURSOR,
    // 快捷键
    ID_KEYLIST = 2200, ID_KEYCAPTURE, ID_KEYRESET_ONE, ID_KEYRESET_ALL, ID_KEYHINT,
    // 远程
    ID_HOSTLIST = 2300, ID_HOSTEDIT, ID_PATHEDIT, ID_HOSTADD, ID_HOSTDEL, ID_HOSTLBL1, ID_HOSTLBL2, ID_HOSTLBL3,
    // 底部
    ID_SAVE = IDOK, ID_CANCEL = IDCANCEL,
};

HWND g_dlg = NULL;
HWND g_tab = NULL;
HWND g_autoStart, g_keyOsd, g_customCursor;
HWND g_keyList, g_keyCapture, g_keyResetOne, g_keyResetAll, g_keyHint;
HWND g_hostList, g_hostEdit, g_pathEdit, g_hostAdd, g_hostDel, g_hostLbl1, g_hostLbl2, g_hostLbl3;
HWND g_pages[3][12]; int g_pageCount[3] = { 0, 0, 0 };

KeyChord g_editMap[(int)Action::Count];
std::vector<RemoteHostInfo> g_editHosts;
bool g_capturing = false;
int  g_captureRow = -1;
WNDPROC g_captureOldProc = NULL;
bool g_zh = false;

const wchar_t* T(const wchar_t* zh, const wchar_t* en) { return g_zh ? zh : en; }

void Track(int page, HWND h) { if (g_pageCount[page] < 12) g_pages[page][g_pageCount[page]++] = h; }

HWND Make(int page, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
    HWND hw = CreateWindowExW(0, cls, text, WS_CHILD | style, x, y, w, h, g_dlg, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
    SendMessage(hw, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    Track(page, hw);
    return hw;
}

void ShowPage(int page) {
    for (int p = 0; p < 3; p++)
        for (int i = 0; i < g_pageCount[p]; i++) ShowWindow(g_pages[p][i], p == page ? SW_SHOW : SW_HIDE);
}

void FillKeyList() {
    ListView_DeleteAllItems(g_keyList);
    for (int i = 0; i < (int)Action::Count; i++) {
        LVITEMW it = {};
        it.mask = LVIF_TEXT; it.iItem = i; it.pszText = (LPWSTR)ActionLabel((Action)i, g_zh);
        ListView_InsertItem(g_keyList, &it);
        std::wstring chord = ChordToString(g_editMap[i]);
        ListView_SetItemText(g_keyList, i, 1, (LPWSTR)chord.c_str());
    }
}

void RefreshKeyRow(int i) {
    std::wstring chord = ChordToString(g_editMap[i]);
    ListView_SetItemText(g_keyList, i, 1, (LPWSTR)chord.c_str());
}

void FillHostList() {
    SendMessage(g_hostList, LB_RESETCONTENT, 0, 0);
    for (const auto& h : g_editHosts) {
        std::wstring label = Utf8ToWide(h.host + "  |  " + h.exePath);
        SendMessageW(g_hostList, LB_ADDSTRING, 0, (LPARAM)label.c_str());
    }
}

// 检查冲突：同一 chord 被两个动作使用（允许 GridBack 与 HistPrev 等不同模式共用）
bool SameContext(Action a, Action b) {
    auto isGridOnly = [](Action x) { return x == Action::GridBack; };
    if (isGridOnly(a) != isGridOnly(b)) return false;
    return true;
}

int FindConflict(int row, const KeyChord& c) {
    for (int i = 0; i < (int)Action::Count; i++)
        if (i != row && g_editMap[i] == c && SameContext((Action)i, (Action)row)) return i;
    return -1;
}

void SetCaptureMode(bool on) {
    g_capturing = on;
    SetWindowTextW(g_keyCapture, on ? T(L"按下新按键…（Esc 取消）", L"Press new key… (Esc to cancel)") : T(L"修改按键", L"Rebind"));
    if (on) SetFocus(g_keyCapture);
}

LRESULT CALLBACK CaptureBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_capturing && (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)) {
        WORD vk = (WORD)wParam;
        if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_LWIN || vk == VK_RWIN) return 0;
        if (vk == VK_ESCAPE) { SetCaptureMode(false); return 0; }
        KeyChord c;
        c.vk = vk;
        c.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        c.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        c.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        // 非 Toggle 类动作不允许带 Ctrl/Alt（会和系统快捷键冲突）
        Action a = (Action)g_captureRow;
        bool isToggle = (a == Action::Toggle || a == Action::ToggleCenter || a == Action::ToggleRemote);
        if (!isToggle && (c.ctrl || c.alt)) {
            MessageBoxW(g_dlg, T(L"普通动作请使用不带 Ctrl/Alt 的按键。", L"Use a key without Ctrl/Alt for regular actions."), L"Vimouse", MB_OK | MB_ICONWARNING);
            return 0;
        }
        if (isToggle && !(c.ctrl || c.alt)) {
            MessageBoxW(g_dlg, T(L"开关类快捷键需要 Ctrl 或 Alt 组合。", L"Toggle hotkeys need Ctrl or Alt."), L"Vimouse", MB_OK | MB_ICONWARNING);
            return 0;
        }
        int conflict = FindConflict(g_captureRow, c);
        if (conflict >= 0) {
            std::wstring msg = std::wstring(T(L"该按键已被「", L"Already used by \"")) + ActionLabel((Action)conflict, g_zh) +
                               T(L"」使用，是否交换？", L"\". Swap?");
            if (MessageBoxW(g_dlg, msg.c_str(), L"Vimouse", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
            g_editMap[conflict] = g_editMap[g_captureRow];
            RefreshKeyRow(conflict);
        }
        g_editMap[g_captureRow] = c;
        RefreshKeyRow(g_captureRow);
        SetCaptureMode(false);
        return 0;
    }
    if (g_capturing && msg == WM_CHAR) return 0;
    return CallWindowProc(g_captureOldProc, hwnd, msg, wParam, lParam);
}

void BuildGeneral() {
    int x = 20, y = 50;
    g_autoStart = Make(0, L"BUTTON", T(L"开机自动启动", L"Run at startup"), WS_VISIBLE | BS_AUTOCHECKBOX, x, y, 380, 24, ID_AUTOSTART); y += 32;
    g_keyOsd = Make(0, L"BUTTON", T(L"屏幕底部显示按键提示（松开后渐隐）", L"Show pressed keys at bottom of screen (fades on release)"), WS_VISIBLE | BS_AUTOCHECKBOX, x, y, 400, 24, ID_KEYOSD); y += 32;
    g_customCursor = Make(0, L"BUTTON", T(L"激活时使用十字准星光标", L"Use crosshair cursor while active"), WS_VISIBLE | BS_AUTOCHECKBOX, x, y, 380, 24, ID_CUSTOMCURSOR); y += 40;
    Make(0, L"STATIC", T(L"配置目录: ", L"Config folder: "), WS_VISIBLE, x, y, 90, 20, 0);
    std::wstring dir = Utf8ToWide(GetConfigDir());
    Make(0, L"EDIT", dir.c_str(), WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL, x + 90, y - 2, 320, 22, 0);

    SendMessage(g_autoStart, BM_SETCHECK, IsAutoStartEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_keyOsd, BM_SETCHECK, g_settings.keyOsd ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessage(g_customCursor, BM_SETCHECK, g_settings.customCursor ? BST_CHECKED : BST_UNCHECKED, 0);
}

void BuildKeys() {
    int x = 20, y = 50;
    g_keyList = Make(1, WC_LISTVIEWW, L"", WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER, x, y, 420, 300, ID_KEYLIST);
    ListView_SetExtendedListViewStyle(g_keyList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = (LPWSTR)T(L"动作", L"Action"); col.cx = 250; ListView_InsertColumn(g_keyList, 0, &col);
    col.pszText = (LPWSTR)T(L"按键", L"Key");    col.cx = 145; ListView_InsertColumn(g_keyList, 1, &col);
    y += 308;
    g_keyCapture = Make(1, L"BUTTON", T(L"修改按键", L"Rebind"), WS_VISIBLE | WS_TABSTOP, x, y, 200, 26, ID_KEYCAPTURE);
    g_keyResetOne = Make(1, L"BUTTON", T(L"恢复此项", L"Reset this"), WS_VISIBLE | WS_TABSTOP, x + 210, y, 100, 26, ID_KEYRESET_ONE);
    g_keyResetAll = Make(1, L"BUTTON", T(L"全部恢复默认", L"Reset all"), WS_VISIBLE | WS_TABSTOP, x + 320, y, 100, 26, ID_KEYRESET_ALL);
    y += 32;
    g_keyHint = Make(1, L"STATIC", T(L"选中一行后点「修改按键」，再按下新键。Shift+移动键固定为精确移动，Shift+左键固定为 Shift 点击。",
                                     L"Select a row, click Rebind, then press the new key. Shift+move is always precise move; Shift+left is always Shift-click."),
                     WS_VISIBLE, x, y, 420, 36, ID_KEYHINT);
    g_captureOldProc = (WNDPROC)SetWindowLongPtr(g_keyCapture, GWLP_WNDPROC, (LONG_PTR)CaptureBtnProc);
    FillKeyList();
}

void BuildRemote() {
    int x = 20, y = 50;
    g_hostLbl1 = Make(2, L"STATIC", T(L"SSH 主机列表（host|远端 Vimouse.exe 路径）:", L"SSH hosts (host | remote Vimouse.exe path):"), WS_VISIBLE, x, y, 420, 20, ID_HOSTLBL1); y += 22;
    g_hostList = Make(2, L"LISTBOX", L"", WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, x, y, 420, 140, ID_HOSTLIST); y += 150;
    g_hostLbl2 = Make(2, L"STATIC", T(L"主机:", L"Host:"), WS_VISIBLE, x, y + 3, 40, 20, ID_HOSTLBL2);
    g_hostEdit = Make(2, L"EDIT", L"", WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, x + 42, y, 150, 24, ID_HOSTEDIT);
    g_hostLbl3 = Make(2, L"STATIC", T(L"路径:", L"Path:"), WS_VISIBLE, x + 200, y + 3, 40, 20, ID_HOSTLBL3);
    g_pathEdit = Make(2, L"EDIT", L"Vimouse.exe", WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, x + 240, y, 180, 24, ID_PATHEDIT); y += 32;
    g_hostAdd = Make(2, L"BUTTON", T(L"添加", L"Add"), WS_VISIBLE | WS_TABSTOP, x, y, 80, 26, ID_HOSTADD);
    g_hostDel = Make(2, L"BUTTON", T(L"删除", L"Remove"), WS_VISIBLE | WS_TABSTOP, x + 90, y, 80, 26, ID_HOSTDEL); y += 40;
    std::wstring hint = std::wstring(T(L"远程开关快捷键: ", L"Remote toggle hotkey: ")) + ChordToString(g_keymap[(int)Action::ToggleRemote]) +
                        T(L"（连接列表中第一台）", L" (connects to the first host)");
    Make(2, L"STATIC", hint.c_str(), WS_VISIBLE, x, y, 420, 20, 0);
    FillHostList();
}

void OnSave() {
    SetAutoStart(SendMessage(g_autoStart, BM_GETCHECK, 0, 0) == BST_CHECKED);
    g_settings.keyOsd = SendMessage(g_keyOsd, BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool cursorBefore = g_settings.customCursor;
    g_settings.customCursor = SendMessage(g_customCursor, BM_GETCHECK, 0, 0) == BST_CHECKED;
    SaveSettings();

    for (int i = 0; i < (int)Action::Count; i++) g_keymap[i] = g_editMap[i];
    SaveKeymap();

    g_remoteHosts = g_editHosts;
    SaveRemoteHosts();

    if (cursorBefore != g_settings.customCursor && g_isActive) {
        if (g_settings.customCursor) SetVimouseCursor(); else RestoreSystemCursor();
    }
    if (g_helpWindow) InvalidateRect(g_helpWindow, NULL, TRUE);
}

LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_dlg = hwnd;
        for (int& c : g_pageCount) c = 0;
        for (int i = 0; i < (int)Action::Count; i++) g_editMap[i] = g_keymap[i];
        LoadRemoteHosts();
        g_editHosts = g_remoteHosts;

        g_tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 10, 10, 445, 425, hwnd, (HMENU)ID_TAB, GetModuleHandle(NULL), NULL);
        SendMessage(g_tab, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        TCITEMW ti = {}; ti.mask = TCIF_TEXT;
        ti.pszText = (LPWSTR)T(L"常规", L"General");  TabCtrl_InsertItem(g_tab, 0, &ti);
        ti.pszText = (LPWSTR)T(L"快捷键", L"Keys");    TabCtrl_InsertItem(g_tab, 1, &ti);
        ti.pszText = (LPWSTR)T(L"远程", L"Remote");    TabCtrl_InsertItem(g_tab, 2, &ti);

        BuildGeneral();
        BuildKeys();
        BuildRemote();
        ShowPage(0);

        HWND ok = CreateWindowExW(0, L"BUTTON", T(L"保存", L"Save"), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 270, 445, 85, 28, hwnd, (HMENU)IDOK, GetModuleHandle(NULL), NULL);
        HWND cancel = CreateWindowExW(0, L"BUTTON", T(L"取消", L"Cancel"), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 365, 445, 85, 28, hwnd, (HMENU)IDCANCEL, GetModuleHandle(NULL), NULL);
        SendMessage(ok, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        SendMessage(cancel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lParam;
        if (nm->idFrom == ID_TAB && nm->code == TCN_SELCHANGE) { ShowPage(TabCtrl_GetCurSel(g_tab)); SetCaptureMode(false); }
        else if (nm->idFrom == ID_KEYLIST && nm->code == NM_DBLCLK) {
            int sel = ListView_GetNextItem(g_keyList, -1, LVNI_SELECTED);
            if (sel >= 0) { g_captureRow = sel; SetCaptureMode(true); }
        }
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_KEYCAPTURE: {
            int sel = ListView_GetNextItem(g_keyList, -1, LVNI_SELECTED);
            if (sel < 0) { MessageBoxW(hwnd, T(L"请先在列表中选中一个动作。", L"Select an action in the list first."), L"Vimouse", MB_OK | MB_ICONINFORMATION); break; }
            g_captureRow = sel;
            SetCaptureMode(!g_capturing);
            break;
        }
        case ID_KEYRESET_ONE: {
            int sel = ListView_GetNextItem(g_keyList, -1, LVNI_SELECTED);
            if (sel < 0) break;
            KeyChord saved[(int)Action::Count];
            for (int i = 0; i < (int)Action::Count; i++) saved[i] = g_keymap[i];
            ResetKeymap();
            g_editMap[sel] = g_keymap[sel];
            for (int i = 0; i < (int)Action::Count; i++) g_keymap[i] = saved[i];
            RefreshKeyRow(sel);
            break;
        }
        case ID_KEYRESET_ALL: {
            KeyChord saved[(int)Action::Count];
            for (int i = 0; i < (int)Action::Count; i++) saved[i] = g_keymap[i];
            ResetKeymap();
            for (int i = 0; i < (int)Action::Count; i++) { g_editMap[i] = g_keymap[i]; g_keymap[i] = saved[i]; }
            FillKeyList();
            break;
        }
        case ID_HOSTADD: {
            wchar_t host[256] = {}, path[512] = {};
            GetWindowTextW(g_hostEdit, host, 256);
            GetWindowTextW(g_pathEdit, path, 512);
            if (host[0]) {
                g_editHosts.push_back({ WideToUtf8(host), path[0] ? WideToUtf8(path) : "Vimouse.exe" });
                FillHostList();
                SetWindowTextW(g_hostEdit, L"");
            }
            break;
        }
        case ID_HOSTDEL: {
            int sel = (int)SendMessage(g_hostList, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR && sel < (int)g_editHosts.size()) { g_editHosts.erase(g_editHosts.begin() + sel); FillHostList(); }
            break;
        }
        case IDOK:
            OnSave();
            DestroyWindow(hwnd);
            break;
        case IDCANCEL:
            DestroyWindow(hwnd);
            break;
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_dlg = NULL;
        g_capturing = false;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

}  // namespace

void ShowSettingsDialog(HWND owner) {
    if (g_dlg) { SetForegroundWindow(g_dlg); return; }
    g_zh = IsSystemChinese();

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = DlgProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"VimouseSettings";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_VIMOUSE));
        RegisterClassExW(&wc);
        registered = true;
    }

    RECT rc = { 0, 0, 465, 485 };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2, y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    HWND hwnd = CreateWindowExW(0, L"VimouseSettings", g_zh ? L"Vimouse 设置" : L"Vimouse Settings",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, x, y, w, h, owner, NULL, GetModuleHandle(NULL), NULL);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
}
