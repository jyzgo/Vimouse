// Main.cpp - 入口、托盘、主窗口消息循环
#include "State.h"
#include "Config.h"
#include "Keymap.h"
#include "Screens.h"
#include "Cursor.h"
#include "Remote.h"
#include "Mover.h"
#include "Tags.h"
#include "Hint.h"
#include "Grid.h"
#include "Indicator.h"
#include "KeyOsd.h"
#include "HelpOverlay.h"
#include "SettingsDialog.h"
#include "Hook.h"
#include "PipeServer.h"
#include "Util.h"
#include "resource.h"
#include <shellapi.h>
#include <cstdio>
#include <string>

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

static NOTIFYICONDATAW g_nid = {};

static void AddTrayIcon(HWND hwnd) {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_VIMOUSE));
    wcscpy_s(g_nid.szTip, L"Vimouse");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void ShowTrayMenu(HWND hwnd) {
    bool zh = IsSystemChinese();
    POINT pt; GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();

    std::wstring toggle = std::wstring(zh ? L"启用键盘控制 (" : L"Enable keyboard control (") + ChordToString(g_keymap[(int)Action::Toggle]) + L")";
    AppendMenuW(menu, g_isActive ? MF_CHECKED : MF_UNCHECKED, IDM_TRAY_TOGGLE, toggle.c_str());

    LoadRemoteHosts();
    if (!g_remoteHosts.empty() || g_remoteMode) {
        HMENU remote = CreatePopupMenu();
        if (g_remoteMode) {
            std::wstring label = std::wstring(zh ? L"断开: " : L"Disconnect: ") + Utf8ToWide(g_remoteHost);
            AppendMenuW(remote, MF_STRING, IDM_TRAY_REMOTE_BASE, label.c_str());
        } else {
            for (int i = 0; i < (int)g_remoteHosts.size() && i < 50; i++)
                AppendMenuW(remote, MF_STRING, IDM_TRAY_REMOTE_BASE + 1 + i, Utf8ToWide(g_remoteHosts[i].host).c_str());
        }
        std::wstring label = std::wstring(zh ? L"远程连接 (" : L"Remote (") + ChordToString(g_keymap[(int)Action::ToggleRemote]) + L")";
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)remote, label.c_str());
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, g_settings.keyOsd ? MF_CHECKED : MF_UNCHECKED, IDM_TRAY_KEYOSD, zh ? L"按键提示 (屏幕底部)" : L"Key OSD (bottom of screen)");
    AppendMenuW(menu, g_helpVisible ? MF_CHECKED : MF_UNCHECKED, IDM_TRAY_HELPWIN, zh ? L"悬浮帮助" : L"Help overlay");
    AppendMenuW(menu, MF_STRING, IDM_TRAY_HELP, zh ? L"操作指南" : L"Quick guide");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_TRAY_SETTINGS, zh ? L"设置..." : L"Settings...");
    AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, zh ? L"退出" : L"Exit");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    PostMessage(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        RefreshScreens();
        CreateGridWindow();
        CreateHintWindow();
        CreateIndicatorWindow();
        CreateHelpWindow();
        KeyOsd_Create();
        LoadTags();
        LoadRemoteHosts();
        StartPipeServer();
        AddTrayIcon(hwnd);
        if (!InstallKeyboardHook()) {
            MessageBoxW(NULL, IsSystemChinese() ? L"设置键盘钩子失败" : L"Failed to install keyboard hook", L"Vimouse", MB_OK | MB_ICONERROR);
            PostQuitMessage(1);
        }
        if (g_isActive) SetVimouseCursor();
        UpdateIndicatorPosition();
        return 0;

    case WM_DISPLAYCHANGE:
        RefreshScreens();
        return 0;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) ShowTrayMenu(hwnd);
        else if (lParam == WM_LBUTTONDBLCLK) ToggleActive(false);
        return 0;

    case WM_APP_SET_ACTIVE:
        SetActive(wParam != 0);
        return 0;

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        if (id == IDM_TRAY_TOGGLE)        ToggleActive(false);
        else if (id == IDM_TRAY_HELP)     ShowHelpDialog(hwnd);
        else if (id == IDM_TRAY_HELPWIN)  ToggleHelpWindow();
        else if (id == IDM_TRAY_KEYOSD)   { g_settings.keyOsd = !g_settings.keyOsd; SaveSettings(); if (!g_settings.keyOsd) KeyOsd_HideNow(); }
        else if (id == IDM_TRAY_SETTINGS) ShowSettingsDialog(hwnd);
        else if (id == IDM_TRAY_EXIT)     DestroyWindow(hwnd);
        else if (id == IDM_TRAY_REMOTE_BASE) { StopRemoteMode(); UpdateIndicatorPosition(); }
        else if (id > IDM_TRAY_REMOTE_BASE && id <= IDM_TRAY_REMOTE_BASE + 50) {
            int idx = id - IDM_TRAY_REMOTE_BASE - 1;
            if (idx < (int)g_remoteHosts.size() && StartRemoteMode(g_remoteHosts[idx].host, g_remoteHosts[idx].exePath)) SetActive(true);
            UpdateIndicatorPosition();
        }
        return 0;
    }

    case WM_DESTROY:
        StopRemoteMode();
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        StopPipeServer();
        StopSmoothMove();
        UninstallKeyboardHook();
        DestroyCustomCursors();
        if (g_gridWindow)      DestroyWindow(g_gridWindow);
        if (g_hintWindow)      DestroyWindow(g_hintWindow);
        if (g_indicatorWindow) DestroyWindow(g_indicatorWindow);
        if (g_helpWindow)      DestroyWindow(g_helpWindow);
        DestroyAllTags();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// 去掉首尾空白和引号
static std::string Trim(std::string s) {
    size_t a = s.find_first_not_of(" \t\"");
    size_t b = s.find_last_not_of(" \t\"");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

// 命令行模式：-c "cmd" / -f script / --pipe-stdin。返回 -1 表示不是 CLI 模式。
static int RunCli(const std::string& args) {
    if (args.empty()) return -1;
    bool isCli = args.rfind("-c", 0) == 0 || args.rfind("-f", 0) == 0 || args.find("--pipe-stdin") != std::string::npos;
    if (!isCli) return -1;

    // GUI 子系统没有控制台：若父进程给了重定向句柄（管道/文件/ssh）就直接用，否则挂到父控制台
    auto valid = [](DWORD id) { HANDLE h = GetStdHandle(id); return h != NULL && h != INVALID_HANDLE_VALUE; };
    bool needConsole = !valid(STD_OUTPUT_HANDLE) || !valid(STD_INPUT_HANDLE);
    if (needConsole) AttachConsole(ATTACH_PARENT_PROCESS);
    FILE* f;
    if (!valid(STD_OUTPUT_HANDLE)) freopen_s(&f, "CONOUT$", "w", stdout);
    if (!valid(STD_ERROR_HANDLE))  freopen_s(&f, "CONOUT$", "w", stderr);
    if (!valid(STD_INPUT_HANDLE))  freopen_s(&f, "CONIN$", "r", stdin);

    if (args.find("--pipe-stdin") != std::string::npos) return RunPipeStdinBridge();
    if (args.rfind("-c", 0) == 0) return RunCLIClient(Trim(args.substr(2)));
    return RunCLIScript(Trim(args.substr(2)));
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int) {
    int cli = RunCli(lpCmdLine ? lpCmdLine : "");
    if (cli >= 0) return cli;

    HANDLE mutex = CreateMutexW(NULL, TRUE, L"VimouseSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, IsSystemChinese() ? L"Vimouse 已经在运行中。" : L"Vimouse is already running.", L"Vimouse", MB_OK | MB_ICONINFORMATION);
        CloseHandle(mutex);
        return 1;
    }

    LoadSettings();
    LoadKeymap();

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"VimouseMain";
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_VIMOUSE));
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(0, L"VimouseMain", L"Vimouse", WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    if (!g_hwnd) { CloseHandle(mutex); return 1; }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    CloseHandle(mutex);
    return (int)msg.wParam;
}
