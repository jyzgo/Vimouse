#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <map>

// 全局变量
HHOOK g_keyboardHook = NULL;
bool g_isActive = true;  // 默认激活状态
int g_mouseSpeed = 10;  // 基础移动速度
int g_wheelSpeed = 300;
bool g_isDragging = false;  // 是否正在拖动
POINT g_lastMousePos;  // 记录鼠标拖动开始位置
int g_currentScreenIndex = 0;  // 当前屏幕索引
std::vector<RECT> g_screenRects;  // 存储所有屏幕的矩形区域
bool g_firstCPress = true;  // 标记是否是第一次按C键
bool g_hintMode = false;  // 是否处于hint模式
bool g_wheelMode = false;  // 是否处于滚轮模式
bool g_gridMode = false;  // 是否处于grid模式
std::vector<RECT> g_gridStack;  // 存储grid模式下的区域栈
HWND g_hintWindow = NULL;  // 单个窗口绘制所有hint
std::string g_currentHint = "";  // 当前输入的hint字符
int g_hintScreenIndex = 0;  // hint模式所在的屏幕索引
bool g_lastActionWasC = false;  // 记录上一个操作是否是C键
HWND g_indicatorWindow = NULL;  // 状态指示器窗口
bool g_leftButtonDown = false;  // 左键按下状态
bool g_rightButtonDown = false;  // 右键按下状态
HWND g_gridWindow = NULL;  // Grid模式窗口
HWND g_hwnd = NULL;  // 主窗口句柄
NOTIFYICONDATA g_nid;  // 托盘图标数据
bool g_exitRequested = false;  // 退出标志

// Ctrl键状态跟踪
bool g_ctrlPressed = false;  // 跟踪Ctrl键状态
bool g_winPressed = false;   // 跟踪Win键状态

// 平滑移动相关变量
bool g_hPressed = false;
bool g_jPressed = false;
bool g_kPressed = false;
bool g_lPressed = false;
std::thread* g_moveThread = nullptr;
bool g_shouldMove = false;

// 加速相关变量
int g_originalSpeed = 15;  // 记录原始速度
bool g_isAccelerating = false;  // 是否正在加速
int g_acceleratedSpeed = 10;  // 每次加速的增量
int g_maxSpeed = 2000;  // 最大速度限制
int g_lastSetSpeed = 10;  // 记录最后一次设置的速度值

// 函数声明
void StartSmoothMove();
void StopSmoothMove();
void EnterHintMode();
void ExitHintMode();
void ExitWheelMode();
void EnterGridModeFromCurrentPos();
void EnterGridMode();
void ExitGridMode();
void MoveToGridArea(int direction);
void MoveToGridCorner(int corner);
void ReturnToPreviousGrid();
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK GridWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK HintWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK IndicatorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void UpdateIndicatorPosition();
void CreateGridWindow();
void CreateHintWindow();
void CreateIndicatorWindow();
BOOL CALLBACK EnumDisplayMonitorsProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData);
int GetCurrentScreenIndex();

// 枚举显示器回调函数
BOOL CALLBACK EnumDisplayMonitorsProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfo(hMonitor, &mi)) {
        g_screenRects.push_back(mi.rcWork);  // 使用工作区域（不包括任务栏）
    }
    return TRUE;
}

// 获取鼠标当前所在屏幕的索引
int GetCurrentScreenIndex() {
    POINT cursorPos;
    GetCursorPos(&cursorPos);

    for (size_t i = 0; i < g_screenRects.size(); i++) {
        if (PtInRect(&g_screenRects[i], cursorPos)) {
            return i;
        }
    }
    // 如果找不到鼠标所在屏幕（理论上不应该发生），返回第一个屏幕
    return 0;
}

// 平滑移动线程函数
void SmoothMoveThread() {
    while (g_shouldMove) {
        POINT currentPos;
        GetCursorPos(&currentPos);
        int newX = currentPos.x;
        int newY = currentPos.y;
        bool moved = false;

        // 检查是否需要加速
        if (g_hPressed || g_jPressed || g_kPressed || g_lPressed) {
            // 动态加速：每0.01秒增加速度
            static DWORD lastMoveTime = GetTickCount();
            static DWORD accelerationStartTime = GetTickCount();

            if (GetTickCount() - accelerationStartTime > 5) { // 每10ms加速一次
                g_mouseSpeed += g_acceleratedSpeed;
                if (g_mouseSpeed > g_maxSpeed) {
                    g_mouseSpeed = g_maxSpeed;
                }
                accelerationStartTime = GetTickCount();
            }
        }

        if (g_hPressed) {
            newX -= g_mouseSpeed / 10;  // 分成10份移动，实现平滑效果
            moved = true;
        }
        if (g_jPressed) {
            newY += g_mouseSpeed / 10;
            moved = true;
        }
        if (g_kPressed) {
            newY -= g_mouseSpeed / 10;
            moved = true;
        }
        if (g_lPressed) {
            newX += g_mouseSpeed / 10;
            moved = true;
        }

        if (moved) {
            SetCursorPos(newX, newY);

            // 如果正在拖动，继续发送拖动事件
            if (g_isDragging) {
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            }
        }

        // 控制移动频率，使移动更平滑
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// 开始平滑移动
void StartSmoothMove() {
    if (!g_shouldMove) {
        g_shouldMove = true;
        g_moveThread = new std::thread(SmoothMoveThread);
    }
}

// 停止平滑移动
void StopSmoothMove() {
    if (g_shouldMove) {
        g_shouldMove = false;
        if (g_moveThread && g_moveThread->joinable()) {
            g_moveThread->join();
            delete g_moveThread;
            g_moveThread = nullptr;
        }
        // 如果正在加速，恢复到上次设置的速度
        g_mouseSpeed = g_lastSetSpeed;
        g_isAccelerating = false;
        UpdateIndicatorPosition();
    }
}

// 创建Grid窗口的窗口过程
LRESULT CALLBACK GridWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // 获取窗口客户区大小
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        // 创建内存DC进行双缓冲绘制
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc,
            clientRect.right - clientRect.left,
            clientRect.bottom - clientRect.top);
        HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);

        // 绘制黑色半透明背景
        HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(memDC, &clientRect, blackBrush);
        DeleteObject(blackBrush);

        // 设置文本颜色和模式
        SetTextColor(memDC, RGB(255, 255, 255));
        SetBkMode(memDC, TRANSPARENT);
        HFONT font = CreateFont(
            20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
        HGDIOBJ oldFont = SelectObject(memDC, font);

        // 绘制Grid线
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        HPEN oldPen = (HPEN)SelectObject(memDC, pen);

        // 计算当前显示区域的中心线
        // 如果有区域栈，使用栈顶的区域；否则使用整个窗口区域
        RECT currentRegion;
        if (g_gridStack.empty()) {
            currentRegion = clientRect;
        }
        else {
            // 将屏幕坐标转换为窗口坐标
            RECT topRegion = g_gridStack.back();
            currentRegion.left = 0;
            currentRegion.top = 0;
            currentRegion.right = clientRect.right - clientRect.left;
            currentRegion.bottom = clientRect.bottom - clientRect.top;
        }

        // 计算中心点（相对于当前窗口客户区）
        int midX = (currentRegion.left + currentRegion.right) / 2;
        int midY = (currentRegion.top + currentRegion.bottom) / 2;

        // 绘制垂直线
        MoveToEx(memDC, midX, 0, NULL);
        LineTo(memDC, midX, currentRegion.bottom);

        // 绘制水平线
        MoveToEx(memDC, 0, midY, NULL);
        LineTo(memDC, currentRegion.right, midY);

        // 恢复画笔和字体并删除
        SelectObject(memDC, oldPen);
        DeleteObject(pen);
        SelectObject(memDC, oldFont);
        DeleteObject(font);

        // 将内存DC内容复制到实际DC
        BitBlt(hdc, 0, 0,
            clientRect.right - clientRect.left,
            clientRect.bottom - clientRect.top,
            memDC, 0, 0, SRCCOPY);

        // 清理资源
        SelectObject(memDC, oldFont);
        DeleteObject(memBitmap);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        break;
    }
    case WM_LBUTTONDOWN: {
        // 点击窗口处理
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        // 计算点击的格子
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        int blockWidth = (clientRect.right - clientRect.left) / 26;
        int blockHeight = (clientRect.bottom - clientRect.top) / 26;

        int col = x / blockWidth;
        int row = y / blockHeight;

        if (col >= 0 && col < 26 && row >= 0 && row < 26) {
            // 移动鼠标到对应位置
            if (g_hintScreenIndex >= 0 && g_hintScreenIndex < (int)g_screenRects.size()) {
                const RECT& screenRect = g_screenRects[g_hintScreenIndex];
                int finalX = screenRect.left + col * blockWidth + blockWidth / 2;
                int finalY = screenRect.top + row * blockHeight + blockHeight / 2;

                SetCursorPos(finalX, finalY);
            }

            // 退出hint模式
            g_hintMode = false;
            g_currentHint = "";
            ShowWindow(hwnd, SW_HIDE);
        }
        break;
    }
    case WM_RBUTTONDOWN:
        // 右键退出hint模式
        g_hintMode = false;
        g_currentHint = "";
        ShowWindow(hwnd, SW_HIDE);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// 创建Hint窗口的窗口过程
LRESULT CALLBACK HintWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // 获取窗口客户区大小
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        // 创建内存DC进行双缓冲绘制
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc,
            clientRect.right - clientRect.left,
            clientRect.bottom - clientRect.top);
        HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);

        // 绘制黑色背景
        HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(memDC, &clientRect, blackBrush);
        DeleteObject(blackBrush);

        // 计算每个格子的大小
        int blockWidth = (clientRect.right - clientRect.left) / 26;
        int blockHeight = (clientRect.bottom - clientRect.top) / 26;

        // 设置文本颜色和模式
        SetTextColor(memDC, RGB(255, 255, 255));
        SetBkMode(memDC, TRANSPARENT);
        HFONT font = CreateFont(
            blockHeight / 2, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial");
        HGDIOBJ oldFont = SelectObject(memDC, font);

        // 绘制所有hint字符
        for (int row = 0; row < 26; row++) {
            for (int col = 0; col < 26; col++) {
                // 如果是hint模式下第一个字母，只显示匹配列
                if (g_currentHint.length() == 1) {
                    if (col != (g_currentHint[0] - 'a')) {
                        continue; // 跳过不匹配的列
                    }
                }

                char hintText[3];
                hintText[0] = 'a' + col;
                hintText[1] = 'a' + row;
                hintText[2] = '\0';

                RECT textRect = {
                    col * blockWidth,
                    row * blockHeight,
                    (col + 1) * blockWidth,
                    (row + 1) * blockHeight
                };

                DrawTextA(memDC, hintText, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }

        // 恢复字体并删除
        SelectObject(memDC, oldFont);
        DeleteObject(font);

        // 将内存DC内容复制到实际DC
        BitBlt(hdc, 0, 0,
            clientRect.right - clientRect.left,
            clientRect.bottom - clientRect.top,
            memDC, 0, 0, SRCCOPY);

        // 清理资源
        SelectObject(memDC, oldFont);
        DeleteObject(memBitmap);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        break;
    }
    case WM_LBUTTONDOWN: {
        // 点击窗口处理
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);

        // 计算点击的格子
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        int blockWidth = (clientRect.right - clientRect.left) / 26;
        int blockHeight = (clientRect.bottom - clientRect.top) / 26;

        int col = x / blockWidth;
        int row = y / blockHeight;

        if (col >= 0 && col < 26 && row >= 0 && row < 26) {
            // 移动鼠标到对应位置
            if (g_gridMode && g_gridStack.size() > 0) {
                // Grid模式下，移动到对应区域
                RECT currentRect = g_gridStack.back();
                int regionWidth = currentRect.right - currentRect.left;
                int regionHeight = currentRect.bottom - currentRect.top;

                int finalX = currentRect.left + col * (regionWidth / 26) + (regionWidth / 26) / 2;
                int finalY = currentRect.top + row * (regionHeight / 26) + (regionHeight / 26) / 2;

                SetCursorPos(finalX, finalY);
            }
            else if (g_hintScreenIndex >= 0 && g_hintScreenIndex < (int)g_screenRects.size()) {
                const RECT& screenRect = g_screenRects[g_hintScreenIndex];
                int finalX = screenRect.left + col * blockWidth + blockWidth / 2;
                int finalY = screenRect.top + row * blockHeight + blockHeight / 2;

                SetCursorPos(finalX, finalY);
            }

            // 退出hint模式
            g_hintMode = false;
            g_currentHint = "";
            ShowWindow(hwnd, SW_HIDE);
        }
        break;
    }
    case WM_RBUTTONDOWN:
        // 右键退出hint模式
        g_hintMode = false;
        g_currentHint = "";
        ShowWindow(hwnd, SW_HIDE);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// 状态指示器窗口过程
LRESULT CALLBACK IndicatorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // 绘制方块，颜色根据鼠标按键状态决定
        RECT rect;
        GetClientRect(hwnd, &rect);

        HBRUSH brush;
        if (g_wheelMode) {
            brush = CreateSolidBrush(RGB(0, 0, 255));  // 蓝色表示滚轮模式
        }
        else if (g_gridMode) {
            brush = CreateSolidBrush(RGB(128, 0, 128));  // 紫色表示grid模式
        }
        else if (g_leftButtonDown) {
            brush = CreateSolidBrush(RGB(0, 255, 0));  // 绿色
        }
        else if (g_rightButtonDown) {
            brush = CreateSolidBrush(RGB(255, 255, 0));  // 黄色
        }
        else {
            brush = CreateSolidBrush(RGB(255, 0, 0));  // 红色
        }

        FillRect(hdc, &rect, brush);
        DeleteObject(brush);

        EndPaint(hwnd, &ps);
        break;
    }
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// 更新指示器位置
void UpdateIndicatorPosition() {
    if (g_indicatorWindow && g_isActive && !g_hintMode && !g_gridMode) {
        // 获取当前鼠标位置
        POINT mousePos;
        GetCursorPos(&mousePos);

        // 将指示器移动到鼠标位置附近（稍微偏移一点，避免遮挡）
        MoveWindow(
            g_indicatorWindow,
            mousePos.x + 10,  // 在鼠标右下方
            mousePos.y + 10,
            14, 6,  // 8x8像素
            TRUE
        );

        // 确保可见
        ShowWindow(g_indicatorWindow, SW_SHOW);

        // 触发重绘以更新颜色
        InvalidateRect(g_indicatorWindow, NULL, TRUE);
    }
    else if (g_indicatorWindow) {
        // 如果不激活或在hint模式/grid模式下则隐藏指示器
        ShowWindow(g_indicatorWindow, SW_HIDE);
    }
}

// 创建grid窗口
void CreateGridWindow() {
    // 注册grid窗口类
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = GridWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"GridWindowClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassEx(&wc);

    // 创建grid窗口，初始时隐藏
    g_gridWindow = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED,  // 置顶且支持透明
        L"GridWindowClass",
        NULL,
        WS_POPUP,
        0, 0, 0, 0,  // 初始位置设为0,0，大小为0,0
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (g_gridWindow) {
        // 设置窗口透明度
        SetLayeredWindowAttributes(g_gridWindow, 0, 150, LWA_ALPHA);
        ShowWindow(g_gridWindow, SW_HIDE);
    }
}

// 创建hint窗口
void CreateHintWindow() {
    // 注册hint窗口类
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = HintWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"HintWindowClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassEx(&wc);

    // 创建单个窗口，初始时隐藏
    g_hintWindow = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED,  // 置顶且支持透明
        L"HintWindowClass",
        NULL,
        WS_POPUP,
        0, 0, 0, 0,  // 初始位置设为0,0，大小为0,0
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (g_hintWindow) {
        // 设置窗口透明度
        SetLayeredWindowAttributes(g_hintWindow, 0, 100, LWA_ALPHA);
        ShowWindow(g_hintWindow, SW_HIDE);
    }
}

// 创建指示器窗口
void CreateIndicatorWindow() {
    // 注册指示器窗口类
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = IndicatorWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"IndicatorWndClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassEx(&wc);

    // 创建指示器窗口，初始时隐藏
    g_indicatorWindow = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED,  // 置顶且支持透明
        L"IndicatorWndClass",
        NULL,
        WS_POPUP,
        0, 0, 8, 8,  // 8x8像素的红色方块
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (g_indicatorWindow) {
        // 设置窗口透明度
        SetLayeredWindowAttributes(g_indicatorWindow, 0, 200, LWA_ALPHA);

        // 初始时隐藏指示器
        ShowWindow(g_indicatorWindow, SW_HIDE);
    }
}

// 进入hint模式
void EnterHintMode() {
    if (g_hintMode) return;  // 已经在hint模式中

    g_hintMode = true;
    g_currentHint = "";
    g_hintScreenIndex = GetCurrentScreenIndex();

    // 检查是否有屏幕信息
    if (g_screenRects.empty()) {
        // 如果没有枚举到屏幕，使用主屏幕
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        g_hintScreenIndex = 0;

        // 创建一个虚拟的屏幕矩形
        RECT mainScreenRect = { 0, 0, screenWidth, screenHeight };
        g_screenRects.push_back(mainScreenRect);
    }

    if (g_hintScreenIndex >= 0 && g_hintScreenIndex < (int)g_screenRects.size()) {
        const RECT& screenRect = g_screenRects[g_hintScreenIndex];

        // 更新窗口位置和大小
        MoveWindow(
            g_hintWindow,
            screenRect.left,
            screenRect.top,
            screenRect.right - screenRect.left,
            screenRect.bottom - screenRect.top,
            TRUE
        );
    }

    ShowWindow(g_hintWindow, SW_SHOW);
    UpdateWindow(g_hintWindow);

    // 隐藏指示器
    if (g_indicatorWindow) {
        ShowWindow(g_indicatorWindow, SW_HIDE);
    }
}

// 退出hint模式
void ExitHintMode(bool isShowSubGrid = true) {
    if (!g_hintMode) return;

    g_hintMode = false;
    g_currentHint = "";

    ShowWindow(g_hintWindow, SW_HIDE);

    // 恢复指示器
    UpdateIndicatorPosition();

    // 退出Hint状态时，鼠标速度调整为15
    g_mouseSpeed = 15;
    g_lastSetSpeed = 15;

    if (isShowSubGrid) {
        // 在鼠标当前位置进入Grid模式，创建180x100的子grid区域
        //EnterGridModeFromCurrentPos();
    }
}

// 退出滚轮模式
void ExitWheelMode() {
    if (!g_wheelMode) return;

    g_wheelMode = false;

    // 恢复指示器
    UpdateIndicatorPosition();
}

// 进入grid模式（以当前鼠标位置为中心创建180x100区域）
void EnterGridModeFromCurrentPos() {
    if (g_gridMode) return;  // 已经在grid模式中

    g_gridMode = true;
    g_gridStack.clear();

    // 获取当前鼠标位置
    POINT currentPos;
    GetCursorPos(&currentPos);

    // 创建一个180x100的区域，以当前鼠标位置为中心
    int regionWidth = 180;
    int regionHeight = 100;
    RECT region = {
        currentPos.x - regionWidth / 2,
        currentPos.y - regionHeight / 2,
        currentPos.x + regionWidth / 2,
        currentPos.y + regionHeight / 2
    };

    // 添加到区域栈
    g_gridStack.push_back(region);

    // 更新grid窗口位置和大小
    MoveWindow(
        g_gridWindow,
        region.left,
        region.top,
        region.right - region.left,
        region.bottom - region.top,
        TRUE
    );

    ShowWindow(g_gridWindow, SW_SHOW);
    UpdateWindow(g_gridWindow);

    // 隐藏指示器
    if (g_indicatorWindow) {
        ShowWindow(g_indicatorWindow, SW_HIDE);
    }
}

// 进入grid模式（以屏幕中央开始）
void EnterGridMode() {
    if (g_gridMode) return;  // 已经在grid模式中

    g_gridMode = true;
    g_gridStack.clear();

    // 获取当前屏幕区域
    int currentScreenIndex = GetCurrentScreenIndex();
    if (currentScreenIndex >= 0 && currentScreenIndex < (int)g_screenRects.size()) {
        RECT screenRect = g_screenRects[currentScreenIndex];
        g_gridStack.push_back(screenRect);

        // 将鼠标移动到屏幕中央
        int centerX = screenRect.left + (screenRect.right - screenRect.left) / 2;
        int centerY = screenRect.top + (screenRect.bottom - screenRect.top) / 2;
        SetCursorPos(centerX, centerY);

        // 更新grid窗口位置和大小
        MoveWindow(
            g_gridWindow,
            screenRect.left,
            screenRect.top,
            screenRect.right - screenRect.left,
            screenRect.bottom - screenRect.top,
            TRUE
        );

        ShowWindow(g_gridWindow, SW_SHOW);
        UpdateWindow(g_gridWindow);
    }

    // 隐藏指示器
    if (g_indicatorWindow) {
        ShowWindow(g_indicatorWindow, SW_HIDE);
    }
}

// 退出grid模式
void ExitGridMode() {
    if (!g_gridMode) return;

    g_gridMode = false;
    g_gridStack.clear();

    ShowWindow(g_gridWindow, SW_HIDE);

    // 恢复指示器
    UpdateIndicatorPosition();
}

// 在grid模式下移动到指定区域
void MoveToGridArea(int direction) {
    if (g_gridStack.empty()) return;

    // 获取当前区域
    RECT currentRect = g_gridStack.back();

    // 计算四个子区域
    int midX = currentRect.left + (currentRect.right - currentRect.left) / 2;
    int midY = currentRect.top + (currentRect.bottom - currentRect.top) / 2;

    RECT newRect;
    int centerX, centerY;

    switch (direction) {
    case 'H': // 左半边区域 (左上 + 左下)
        newRect = { currentRect.left, currentRect.top, midX, currentRect.bottom };
        centerX = newRect.left + (newRect.right - newRect.left) / 2;
        centerY = newRect.top + (newRect.bottom - newRect.top) / 2;
        break;
    case 'J': // 下半边区域 (左下 + 右下)
        newRect = { currentRect.left, midY, currentRect.right, currentRect.bottom };
        centerX = newRect.left + (newRect.right - newRect.left) / 2;
        centerY = newRect.top + (newRect.bottom - newRect.top) / 2;
        break;
    case 'K': // 上半边区域 (左上 + 右上)
        newRect = { currentRect.left, currentRect.top, currentRect.right, midY };
        centerX = newRect.left + (newRect.right - newRect.left) / 2;
        centerY = newRect.top + (newRect.bottom - newRect.top) / 2;
        break;
    case 'L': // 右半边区域 (右上 + 右下)
        newRect = { midX, currentRect.top, currentRect.right, currentRect.bottom };
        centerX = newRect.left + (newRect.right - newRect.left) / 2;
        centerY = newRect.top + (newRect.bottom - newRect.top) / 2;
        break;
    default:
        return;
    }

    // 将鼠标移动到新区域中央
    SetCursorPos(centerX, centerY);

    // 添加到区域栈
    g_gridStack.push_back(newRect);

    // 更新grid窗口位置和大小
    MoveWindow(
        g_gridWindow,
        newRect.left,
        newRect.top,
        newRect.right - newRect.left,
        newRect.bottom - newRect.top,
        TRUE
    );

    ShowWindow(g_gridWindow, SW_SHOW);
    UpdateWindow(g_gridWindow);
}

// 在grid模式下快速定位到角落区域
void MoveToGridCorner(int corner) {
    if (g_gridStack.empty()) return;

    // 获取当前区域
    RECT currentRect = g_gridStack.back();

    // 计算四个角落区域
    int midX = currentRect.left + (currentRect.right - currentRect.left) / 2;
    int midY = currentRect.top + (currentRect.bottom - currentRect.top) / 2;

    RECT newRect;
    int centerX, centerY;

    switch (corner) {
    case 'Q': // 左上角区域
        newRect = { currentRect.left, currentRect.top, midX, midY };
        centerX = newRect.left + (newRect.right - newRect.left) / 2;
        centerY = newRect.top + (newRect.bottom - newRect.top) / 2;
        break;
    case 'W': // 右上角区域
        newRect = { midX, currentRect.top, currentRect.right, midY };
        centerX = newRect.left + (newRect.right - newRect.left) / 2;
        centerY = newRect.top + (newRect.bottom - newRect.top) / 2;
        break;
    case 'A': // 左下角区域
        newRect = { currentRect.left, midY, midX, currentRect.bottom };
        centerX = newRect.left + (newRect.right - newRect.left) / 2;
        centerY = newRect.top + (newRect.bottom - newRect.top) / 2;
        break;
    case 'S': // 右下角区域
        newRect = { midX, midY, currentRect.right, currentRect.bottom };
        centerX = newRect.left + (newRect.right - newRect.left) / 2;
        centerY = newRect.top + (newRect.bottom - newRect.top) / 2;
        break;
    default:
        return;
    }

    // 将鼠标移动到角落区域中央
    SetCursorPos(centerX, centerY);

    // 添加到区域栈
    g_gridStack.push_back(newRect);

    // 更新grid窗口位置和大小
    MoveWindow(
        g_gridWindow,
        newRect.left,
        newRect.top,
        newRect.right - newRect.left,
        newRect.bottom - newRect.top,
        TRUE
    );

    ShowWindow(g_gridWindow, SW_SHOW);
    UpdateWindow(g_gridWindow);
}

// 返回上一个区域
void ReturnToPreviousGrid() {
    if (g_gridStack.size() <= 1) {
        // 如果只有一个区域，退出grid模式
        ExitGridMode();
        return;
    }

    // 移除当前区域
    g_gridStack.pop_back();

    // 获取上一个区域
    if (!g_gridStack.empty()) {
        RECT prevRect = g_gridStack.back();

        // 将鼠标移动到上一个区域中央
        int centerX = prevRect.left + (prevRect.right - prevRect.left) / 2;
        int centerY = prevRect.top + (prevRect.bottom - prevRect.top) / 2;
        SetCursorPos(centerX, centerY);

        // 更新grid窗口位置和大小
        MoveWindow(
            g_gridWindow,
            prevRect.left,
            prevRect.top,
            prevRect.right - prevRect.left,
            prevRect.bottom - prevRect.top,
            TRUE
        );

        ShowWindow(g_gridWindow, SW_SHOW);
        UpdateWindow(g_gridWindow);
    }
}

// 钩子回调函数
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* pKeyboard = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vkCode = pKeyboard->vkCode;
        bool isKeyDown = (wParam == WM_KEYDOWN);
        bool isKeyUp = (wParam == WM_KEYUP);

        // 更新Ctrl键状态
        if (vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL) {
            g_ctrlPressed = isKeyDown;
            return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
        }
        if (g_ctrlPressed && vkCode == VK_OEM_MINUS)
        {
            //不处理vs studio的状态
            return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
        }

        // 更新Win键状态
        if (vkCode == VK_LWIN || vkCode == VK_RWIN) {
            g_winPressed = isKeyDown;
            return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
        }

        // 检查 Win+空格 组合键，确保不被屏蔽
        if (g_winPressed && vkCode == VK_SPACE) {
            // 不阻止Win+空格，让系统正常处理
            return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
        }

        // 检查 Ctrl+\ 切换激活状态
        if (isKeyDown && vkCode == VK_OEM_5 && g_ctrlPressed) {
            g_isActive = !g_isActive;
            if (g_isActive) {
                GetCursorPos(&g_lastMousePos);  // 记录当前位置
                // 重置C键状态
                g_firstCPress = true;
                g_lastActionWasC = false;
                // 重置鼠标按键状态
                g_leftButtonDown = false;
                g_rightButtonDown = false;
                g_wheelMode = false;  // 退出滚轮模式
                g_gridMode = false;  // 退出grid模式
                // 重置Ctrl状态
                g_ctrlPressed = false;
                // 重置Win状态
                g_winPressed = false;
                // 重新枚举显示器
                g_screenRects.clear();
                EnumDisplayMonitors(NULL, NULL, EnumDisplayMonitorsProc, 0);
            }
            // 更新指示器位置
            UpdateIndicatorPosition();
            return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);  // 让Ctrl+\正常工作
        }

        // 检查 Eenter 关闭激活状态,并且在当前区域点击一下鼠标
        if (isKeyDown && vkCode == VK_RETURN && g_isActive) {
            g_isActive = false;
            ExitHintMode(false);  // 退出hint模式
            ExitWheelMode();  // 退出滚轮模式
            ExitGridMode();   // 退出grid模式
            // 更新指示器位置
            g_leftButtonDown = true;  // 设置左键按下状态
            // 更新指示器位置（会触发重绘）
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            g_leftButtonDown = false;  // 设置左键抬起状态

            UpdateIndicatorPosition();
            return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);  // 让Esc正常工作
        }

        // 检查 Esc 关闭激活状态
        if (isKeyDown && vkCode == VK_ESCAPE && g_isActive) {
            if (g_hintMode) {
                ExitHintMode(false);  // 退出hint模式
                return 1;
            }
            g_isActive = false;
            ExitWheelMode();  // 退出滚轮模式
            ExitGridMode();   // 退出grid模式
            // 更新指示器位置
            UpdateIndicatorPosition();
            return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);  // 让Esc正常工作
        }

        // 如果不处于激活状态，直接返回
        if (!g_isActive) {
            return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
        }

        // 如果处于grid模式，处理特定键
        if (g_gridMode) {
            if (isKeyDown) {
                switch (vkCode) {
                case 'H':  // 左半边区域 (左上 + 左下)
                case 'J':  // 下半边区域 (左下 + 右下)
                case 'K':  // 上半边区域 (左上 + 右上)
                case 'L':  // 右半边区域 (右上 + 右下)
                    // 修改：在Grid模式下按HJKL键直接退出Grid模式
                    ExitGridMode();
                    return 1;
                    //return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);  // 让HJKL键正常工作
                case 'Q':  // 左上角区域
                case 'W':  // 右上角区域
                case 'A':  // 左下角区域
                case 'S':  // 右下角区域
                    MoveToGridCorner(vkCode);
                    return 1;  // 阻止其他程序接收到这些按键
                case 'R':  // 返回上一个区域
                    ReturnToPreviousGrid();
                    return 1;
                case 'I':  // 退出grid模式
                case VK_ESCAPE:  // 退出grid模式
                    ExitGridMode();
                    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);  // 让Esc正常工作
                case 'F':  // 左键点击
                {
                    // 在Grid模式下执行点击后，保持当前位置并退出Grid模式

                    // 退出Grid模式
                    if (!g_leftButtonDown) {
                        ExitGridMode();
                        g_leftButtonDown = true;  // 设置左键按下状态
                        // 更新指示器位置（会触发重绘）
                        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                        UpdateIndicatorPosition();
                    }
                }
                return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);  // 让F键正常工作
                case 'G':  // 右键点击
                {
                    // 在Grid模式下执行点击后，保持当前位置并退出Grid模式
                    g_rightButtonDown = true;  // 设置右键按下状态
                    // 更新指示器位置（会触发重绘）
                    UpdateIndicatorPosition();

                    // 退出Grid模式
                    ExitGridMode();
                    mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
                    g_rightButtonDown = false;  // 设置右键抬起状态
                    // 更新指示器位置（会触发重绘）
                    UpdateIndicatorPosition();
                }
                return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);  // 让G键正常工作
                case 'V':  // 拖拽切换
                {
                    // 退出Grid模式
                    ExitGridMode();
                    // 在Grid模式下执行拖拽操作后，保持当前位置并退出Grid模式
                    if (!g_isDragging) {
                        // 开始拖动
                        GetCursorPos(&g_lastMousePos);
                        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                        g_isDragging = true;
                    }
                    else {
                        // 结束拖动
                        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                        g_isDragging = false;
                    }
                    // 更新指示器位置
                    UpdateIndicatorPosition();
                }
                return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);  // 让V键正常工作
                default:
                    // 其他键退出grid模式并继续处理
                    ExitGridMode();
                    return 1;
                    //return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                }
            }
        }

        // 如果处于hint模式，只处理字母键和Esc键
        if (g_hintMode) {
            if (isKeyDown) {
                if ((vkCode >= 'A' && vkCode <= 'Z') || vkCode == VK_ESCAPE) {
                    if (vkCode == VK_ESCAPE) {
                        // Esc键退出hint模式
                        g_hintMode = false;
                        g_currentHint = "";
                        ShowWindow(g_hintWindow, SW_HIDE);
                        // 恢复指示器
                        UpdateIndicatorPosition();
                        // 退出Hint状态时，鼠标速度调整为15
                        g_mouseSpeed = 15;
                        g_lastSetSpeed = 15;
                        // 在鼠标当前位置进入Grid模式，创建180x100的子grid区域
                        EnterGridModeFromCurrentPos();
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);  // 让Esc正常工作
                    }
                    else {
                        // 处理字母键
                        char letter = (char)(vkCode - 'A' + 'a');  // 转换为小写

                        if (g_currentHint.length() == 0) {
                            // 第一个字母，更新显示（只显示匹配列）
                            g_currentHint += letter;
                            InvalidateRect(g_hintWindow, NULL, TRUE);  // 触发重绘
                            return 1;
                        }
                        else if (g_currentHint.length() == 1) {
                            // 第二个字母，定位并退出hint模式
                            g_currentHint += letter;

                            // 检查数组边界
                            if (g_hintScreenIndex >= 0 && g_hintScreenIndex < (int)g_screenRects.size()) {
                                // 找到对应的位置
                                int col = g_currentHint[0] - 'a';
                                int row = g_currentHint[1] - 'a';

                                const RECT& screenRect = g_screenRects[g_hintScreenIndex];
                                int blockWidth = (screenRect.right - screenRect.left) / 26;
                                int blockHeight = (screenRect.bottom - screenRect.top) / 26;

                                int x = screenRect.left + col * blockWidth + blockWidth / 2;
                                int y = screenRect.top + row * blockHeight + blockHeight / 2;

                                SetCursorPos(x, y);
                            }

                            // 退出hint模式
                            ExitHintMode(true);
                        }
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);  // 让字母键正常工作
                    }
                }
                // 对于hint模式下非字母和Esc键，直接阻止
                return 1;
            }
        }
        else {
            // 非hint模式，处理其他按键
            if (isKeyDown) {
                // 检查 Ctrl+F 并执行鼠标左键点击
                if (g_ctrlPressed && vkCode == 'F') {
                    // 执行鼠标左键点击操作
                    g_leftButtonDown = true;  // 设置左键按下状态
                    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                    g_leftButtonDown = false;  // 设置左键抬起状态
                    
                    // 更新指示器位置
                    UpdateIndicatorPosition();
                    
                    // 阻止 Ctrl+F 传递给其他程序
                    return 1;
                }

                // 检查是否为Ctrl+字母组合键，如果是则传递给其他程序
                // 但只对C和V进行特殊处理，其他Ctrl+字母组合直接传递
                if (g_ctrlPressed && (vkCode >= 'A' && vkCode <= 'Z')) {
                    // 特殊处理：Ctrl+C 和 Ctrl+V 需要传递给其他程序
                    if (vkCode == 'C' || vkCode == 'V') {
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    // 其他Ctrl+字母组合也传递给其他程序（如Ctrl+Z, Ctrl+A等）
                    else {
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                }

                // 滚轮模式处理：如果在滚轮模式下，HJKL用于滚轮滚动
                if (g_wheelMode) {
                    POINT currentPos;
                    GetCursorPos(&currentPos);

                    switch (vkCode) {
                    case 'H':  // 左移 -> 水平向左滚动
                        mouse_event(MOUSEEVENTF_HWHEEL, 0, 0, -g_wheelSpeed, 0);
                        break;
                    case 'J':  // 下移 -> 垂直向下滚动
                        mouse_event(MOUSEEVENTF_WHEEL, 0, 0, -g_wheelSpeed, 0);
                        break;
                    case 'K':  // 上移 -> 垂直向上滚动
                        mouse_event(MOUSEEVENTF_WHEEL, 0, 0, g_wheelSpeed, 0);
                        break;
                    case 'L':  // 右移 -> 水平向右滚动
                        mouse_event(MOUSEEVENTF_HWHEEL, 0, 0, g_wheelSpeed, 0);
                        break;
                    case 'U':  // 再次按U键退出滚轮模式
                        ExitWheelMode();
                        break;
                    case 'M':  // 按M键进入hint模式，退出滚轮模式
                        ExitWheelMode();
                        EnterHintMode();
                        break;
                        // 速度键：QWER 不退出滚轮模式，但改变速度
                    case 'Q':
                        g_mouseSpeed = 20;
                        g_lastSetSpeed = 20;
                        break;
                    case 'W':
                        g_mouseSpeed = 80;
                        g_lastSetSpeed = 80;
                        break;
                    case 'E':
                        g_mouseSpeed = 160;
                        g_lastSetSpeed = 160;
                        break;
                    case 'R':
                        g_mouseSpeed = 320;
                        g_lastSetSpeed = 320;
                        break;
                    case 'F':  // 左键点击
                        g_leftButtonDown = true;  // 设置左键按下状态
                        // 更新指示器位置（会触发重绘）
                        UpdateIndicatorPosition();
                        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                        g_leftButtonDown = false;  // 设置左键抬起状态
                        // 更新指示器位置（会触发重绘）
                        UpdateIndicatorPosition();
                        break;
                    case 'G':  // 右键点击
                        g_rightButtonDown = true;  // 设置右键按下状态
                        // 更新指示器位置（会触发重绘）
                        UpdateIndicatorPosition();
                        mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
                        g_rightButtonDown = false;  // 设置右键抬起状态
                        // 更新指示器位置（会触发重绘）
                        UpdateIndicatorPosition();
                        break;
                    case 'V':
                        if (!g_isDragging) {
                            // 开始拖动
                            GetCursorPos(&g_lastMousePos);
                            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                            g_isDragging = true;
                        }
                        else {
                            // 结束拖动
                            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                            g_isDragging = false;
                        }
                        // 更新指示器位置
                        UpdateIndicatorPosition();
                        break;
                    default:
                        // 其他按键退出滚轮模式并处理为普通按键
                        ExitWheelMode();
                        // 重新处理这个按键
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }

                    // 更新指示器位置（会触发重绘）
                    UpdateIndicatorPosition();

                    // 阻止其他程序接收到这些按键
                    return 1;
                }

                UpdateIndicatorPosition();

                // 非滚轮模式下的按键处理
                POINT currentPos;
                GetCursorPos(&currentPos);

                switch (vkCode) {
                case 'H':  // 左移
                    if (g_ctrlPressed) {
                        // Ctrl+H传递给其他程序
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    g_hPressed = true;
                    g_lastActionWasC = false;  // 重置C键状态
                    StartSmoothMove();
                    // 更新指示器位置
                    UpdateIndicatorPosition();
                    break;
                case 'J':  // 下移
                    if (g_ctrlPressed) {
                        // Ctrl+J传递给其他程序
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    g_jPressed = true;
                    g_lastActionWasC = false;  // 重置C键状态
                    StartSmoothMove();
                    // 更新指示器位置
                    UpdateIndicatorPosition();
                    break;
                case 'K':  // 上移
                    if (g_ctrlPressed) {
                        // Ctrl+K传递给其他程序
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    g_kPressed = true;
                    g_lastActionWasC = false;  // 重置C键状态
                    StartSmoothMove();
                    // 更新指示器位置
                    UpdateIndicatorPosition();
                    break;
                case 'L':  // 右移
                    if (g_ctrlPressed) {
                        // Ctrl+L传递给其他程序
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    g_lPressed = true;
                    g_lastActionWasC = false;  // 重置C键状态
                    StartSmoothMove();
                    // 更新指示器位置
                    UpdateIndicatorPosition();
                    break;

                    // 滚轮模式
                case 'U':
                    g_wheelMode = !g_wheelMode;  // 切换滚轮模式
                    if (g_wheelMode) {
                        // 进入滚轮模式
                        g_lastActionWasC = false;  // 重置C键状态
                    }
                    else {
                        // 退出滚轮模式
                        // 恢复指示器
                        UpdateIndicatorPosition();
                    }
                    break;

                    // Grid模式
                case 'I':
                    if (g_ctrlPressed) {
                        // 如果I键是和Ctrl一起按下的，不执行I键功能，直接传递给其他程序
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    EnterGridMode();
                    g_lastActionWasC = false;  // 重置C键状态
                    break;

                    // 速度设置
                case 'Q':
                    g_mouseSpeed = 20;
                    g_lastSetSpeed = 20;
                    break;
                case 'W':
                    g_mouseSpeed = 80;
                    g_lastSetSpeed = 80;
                    break;
                case 'E':
                    g_mouseSpeed = 160;
                    g_lastSetSpeed = 160;
                    break;
                case 'R':
                    g_mouseSpeed = 320;
                    g_lastSetSpeed = 320;
                    break;

                    // 鼠标点击
                case 'F':  // 左键点击
                    if (!g_leftButtonDown) {
                        g_leftButtonDown = true;  // 设置左键按下状态
                        // 更新指示器位置（会触发重绘）
                        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                        UpdateIndicatorPosition();
                        g_lastActionWasC = false;  // 重置C键状态
                    }
                    return 1;
                    break;
                case 'G':  // 右键点击
                    g_rightButtonDown = true;  // 设置右键按下状态
                    // 更新指示器位置（会触发重绘）
                    UpdateIndicatorPosition();
                    mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
                    g_rightButtonDown = false;  // 设置右键抬起状态
                    // 更新指示器位置（会触发重绘）
                    UpdateIndicatorPosition();
                    g_lastActionWasC = false;  // 重置C键状态
                    break;

                    // 拖动控制
                case 'V':
                    if (g_ctrlPressed) {
                        // 如果V键是和Ctrl一起按下的，不执行V键功能，直接传递给其他程序
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    if (!g_isDragging) {
                        // 开始拖动
                        GetCursorPos(&g_lastMousePos);
                        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                        g_isDragging = true;
                    }
                    else {
                        // 结束拖动
                        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                        g_isDragging = false;
                    }
                    g_lastActionWasC = false;  // 重置C键状态
                    // 更新指示器位置
                    UpdateIndicatorPosition();
                    break;

                    // 屏幕中心控制
                case 'C':
                    if (g_ctrlPressed) {
                        // 如果C键是和Ctrl一起按下的，不执行C键功能，直接传递给其他程序
                        return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
                    }
                    if (g_screenRects.empty()) {
                        // 如果没有枚举到屏幕，使用主屏幕
                        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
                        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
                        SetCursorPos(screenWidth / 2, screenHeight / 2);
                    }
                    else {
                        if (g_firstCPress || !g_lastActionWasC) {
                            // 第一次按C或上次操作不是C：移动到当前屏幕中心
                            g_currentScreenIndex = GetCurrentScreenIndex();
                            g_firstCPress = false;
                            g_lastActionWasC = true;
                        }
                        else {
                            // 上次操作是C：切换到下一个屏幕
                            g_currentScreenIndex = (g_currentScreenIndex + 1) % g_screenRects.size();
                        }

                        const RECT& screenRect = g_screenRects[g_currentScreenIndex];
                        int centerX = screenRect.left + (screenRect.right - screenRect.left) / 2;
                        int centerY = screenRect.top + (screenRect.bottom - screenRect.top) / 2;
                        SetCursorPos(centerX, centerY);
                    }
                    // 更新指示器位置
                    UpdateIndicatorPosition();
                    break;

                    // Hint模式
                case 'M':
                    EnterHintMode();
                    g_lastActionWasC = false;  // 重置C键状态
                    break;
                case 'A':
                    return 1;
                case VK_LEFT:
				case VK_UP:
                case VK_RIGHT:
                case VK_DOWN:
                case VK_F5:
					return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);

                default:
                    g_lastActionWasC = false;  // 重置C键状态
                    // 更新指示器位置
                    UpdateIndicatorPosition();
                    break;
                }

                // 阻止其他程序接收到这些按键
                return 1;
            }
            else if (isKeyUp) {
                UpdateIndicatorPosition();
                // 处理按键释放
                switch (vkCode) {
                case 'H':
                    g_hPressed = false;
                    if (!g_hPressed && !g_jPressed && !g_kPressed && !g_lPressed) {
                        StopSmoothMove();
                    }
                    break;
                case 'J':
                    g_jPressed = false;
                    if (!g_hPressed && !g_jPressed && !g_kPressed && !g_lPressed) {
                        StopSmoothMove();
                    }
                    break;
                case 'K':
                    g_kPressed = false;
                    if (!g_hPressed && !g_jPressed && !g_kPressed && !g_lPressed) {
                        StopSmoothMove();
                    }
                    break;
                case 'L':
                    g_lPressed = false;
                    if (!g_hPressed && !g_jPressed && !g_kPressed && !g_lPressed) {
                        StopSmoothMove();
                    }
                    break;
                case 'F':
                    if (g_leftButtonDown)
                    {
                        g_leftButtonDown = false;
                        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                        UpdateIndicatorPosition();
                        
                    }
                    break;
                default:
                    return 0;

                }
            }
        }
    }

    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
}

// 创建托盘图标
void CreateTrayIcon() {
    // 初始化NOTIFYICONDATA结构
    ZeroMemory(&g_nid, sizeof(NOTIFYICONDATA));
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_USER + 1;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);  // 使用默认图标
    wcscpy_s(g_nid.szTip, L"键盘控制鼠标工具");

    // 添加托盘图标
    Shell_NotifyIcon(NIM_ADD, &g_nid);
}

// 删除托盘图标
void RemoveTrayIcon() {
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
}

// 创建一个隐藏窗口来接收消息循环
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        // 重新枚举显示器
        g_screenRects.clear();
        EnumDisplayMonitors(NULL, NULL, EnumDisplayMonitorsProc, 0);

        // 创建grid窗口
        CreateGridWindow();

        // 创建hint窗口
        CreateHintWindow();

        // 创建指示器窗口
        CreateIndicatorWindow();

        // 设置键盘钩子
        g_keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc,
            GetModuleHandle(NULL), 0);
        if (g_keyboardHook == NULL) {
            MessageBox(NULL, L"设置键盘钩子失败", L"错误", MB_OK | MB_ICONERROR);
            PostQuitMessage(1);
        }

        // 创建托盘图标
        CreateTrayIcon();
        break;

    case WM_USER + 1:  // 托盘图标消息
        if (lParam == WM_RBUTTONDOWN) {  // 右键点击托盘图标
            // 创建弹出菜单
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, 1, L"退出");

            // 显示菜单
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);

            // 处理菜单选择
            if (cmd == 1) {  // 退出
                g_exitRequested = true;
                PostQuitMessage(0);
            }

            // 清理菜单
            DestroyMenu(hMenu);
        }
        break;

    case WM_DESTROY:
        // 停止平滑移动
        StopSmoothMove();

        // 卸载钩子
        if (g_keyboardHook) {
            UnhookWindowsHookEx(g_keyboardHook);
        }
        // 销毁grid窗口
        if (g_gridWindow) {
            DestroyWindow(g_gridWindow);
        }
        // 销毁hint窗口
        if (g_hintWindow) {
            DestroyWindow(g_hintWindow);
        }
        // 销毁指示器窗口
        if (g_indicatorWindow) {
            DestroyWindow(g_indicatorWindow);
        }
        // 删除托盘图标
        RemoveTrayIcon();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 检查是否已经有一个实例在运行
    HANDLE hMutex = CreateMutex(NULL, TRUE, L"MouseControllerMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBox(NULL, L"程序已经在运行中！", L"提示", MB_OK | MB_ICONINFORMATION);
        CloseHandle(hMutex);
        return 1;
    }

    // 注册窗口类
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MouseControllerClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, L"注册窗口类失败", L"错误", MB_OK | MB_ICONERROR);
        CloseHandle(hMutex);
        return 1;
    }

    // 创建隐藏窗口
    g_hwnd = CreateWindowEx(
        0,
        L"MouseControllerClass",
        L"键盘控制鼠标工具",
        0,  // 无样式，隐藏窗口
        CW_USEDEFAULT, CW_USEDEFAULT,
        300, 200,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hwnd) {
        MessageBox(NULL, L"创建窗口失败", L"错误", MB_OK | MB_ICONERROR);
        CloseHandle(hMutex);
        return 1;
    }

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (g_exitRequested) break;  // 检查退出标志
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 释放互斥锁
    CloseHandle(hMutex);

    UpdateIndicatorPosition();
    return (int)msg.wParam;
}



