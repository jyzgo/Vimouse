#include "UIAutomation.h"
#include <UIAutomation.h>
#include <thread>
#include <chrono>
#include <comdef.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

static IUIAutomation* g_pAutomation = nullptr;

// 辅助：BSTR 转 UTF-8
static std::string BstrToUtf8(BSTR bstr) {
    if (!bstr) return "";
    int len = SysStringLen(bstr);
    if (len == 0) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, bstr, len, NULL, 0, NULL, NULL);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, bstr, len, &result[0], size, NULL, NULL);
    return result;
}

// 辅助：JSON 转义
static std::string JsonEscape(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 10);
    for (char c : s) {
        switch (c) {
        case '"':  result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:   result += c; break;
        }
    }
    return result;
}

// 辅助：ControlType ID 转可读名称
static std::string ControlTypeName(CONTROLTYPEID id) {
    switch (id) {
    case UIA_ButtonControlTypeId: return "Button";
    case UIA_CalendarControlTypeId: return "Calendar";
    case UIA_CheckBoxControlTypeId: return "CheckBox";
    case UIA_ComboBoxControlTypeId: return "ComboBox";
    case UIA_EditControlTypeId: return "Edit";
    case UIA_HyperlinkControlTypeId: return "Hyperlink";
    case UIA_ImageControlTypeId: return "Image";
    case UIA_ListItemControlTypeId: return "ListItem";
    case UIA_ListControlTypeId: return "List";
    case UIA_MenuControlTypeId: return "Menu";
    case UIA_MenuBarControlTypeId: return "MenuBar";
    case UIA_MenuItemControlTypeId: return "MenuItem";
    case UIA_ProgressBarControlTypeId: return "ProgressBar";
    case UIA_RadioButtonControlTypeId: return "RadioButton";
    case UIA_ScrollBarControlTypeId: return "ScrollBar";
    case UIA_SliderControlTypeId: return "Slider";
    case UIA_SpinnerControlTypeId: return "Spinner";
    case UIA_StatusBarControlTypeId: return "StatusBar";
    case UIA_TabControlTypeId: return "Tab";
    case UIA_TabItemControlTypeId: return "TabItem";
    case UIA_TextControlTypeId: return "Text";
    case UIA_ToolBarControlTypeId: return "ToolBar";
    case UIA_ToolTipControlTypeId: return "ToolTip";
    case UIA_TreeControlTypeId: return "Tree";
    case UIA_TreeItemControlTypeId: return "TreeItem";
    case UIA_WindowControlTypeId: return "Window";
    case UIA_PaneControlTypeId: return "Pane";
    case UIA_HeaderControlTypeId: return "Header";
    case UIA_HeaderItemControlTypeId: return "HeaderItem";
    case UIA_TableControlTypeId: return "Table";
    case UIA_TitleBarControlTypeId: return "TitleBar";
    case UIA_SeparatorControlTypeId: return "Separator";
    default: return "Unknown(" + std::to_string(id) + ")";
    }
}

// 辅助：获取元素信息 JSON
static std::string ElementInfoJson(IUIAutomationElement* elem) {
    BSTR name = nullptr;
    elem->get_CurrentName(&name);
    std::string nameStr = BstrToUtf8(name);
    if (name) SysFreeString(name);

    CONTROLTYPEID typeId;
    elem->get_CurrentControlType(&typeId);
    std::string typeName = ControlTypeName(typeId);

    RECT rect = {};
    elem->get_CurrentBoundingRectangle(&rect);

    BOOL enabled = FALSE;
    elem->get_CurrentIsEnabled(&enabled);

    std::string json = "{";
    json += "\"name\":\"" + JsonEscape(nameStr) + "\"";
    json += ",\"type\":\"" + typeName + "\"";
    json += ",\"rect\":[" + std::to_string(rect.left) + ","
        + std::to_string(rect.top) + ","
        + std::to_string(rect.right) + ","
        + std::to_string(rect.bottom) + "]";
    json += ",\"enabled\":" + std::string(enabled ? "true" : "false");
    json += "}";
    return json;
}

bool InitUIAutomation() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;

    hr = CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER,
        __uuidof(IUIAutomation), (void**)&g_pAutomation);
    return SUCCEEDED(hr) && g_pAutomation != nullptr;
}

void CleanupUIAutomation() {
    if (g_pAutomation) {
        g_pAutomation->Release();
        g_pAutomation = nullptr;
    }
    CoUninitialize();
}

std::string FindUIElement(const std::string& name, const std::string& type) {
    if (!g_pAutomation) return "ERR UI Automation not initialized";

    HWND fgWnd = GetForegroundWindow();
    if (!fgWnd) return "ERR no foreground window";

    IUIAutomationElement* root = nullptr;
    HRESULT hr = g_pAutomation->ElementFromHandle(fgWnd, &root);
    if (FAILED(hr) || !root) return "ERR failed to get root element";

    // 创建搜索条件: name 匹配
    int nameLen = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, NULL, 0);
    std::wstring wName(nameLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, &wName[0], nameLen);

    VARIANT varName;
    varName.vt = VT_BSTR;
    varName.bstrVal = SysAllocString(wName.c_str());

    IUIAutomationCondition* nameCond = nullptr;
    g_pAutomation->CreatePropertyCondition(UIA_NamePropertyId, varName, &nameCond);
    SysFreeString(varName.bstrVal);

    if (!nameCond) {
        root->Release();
        return "ERR failed to create search condition";
    }

    // 如果指定了 type，创建组合条件
    IUIAutomationCondition* finalCond = nameCond;
    IUIAutomationCondition* typeCond = nullptr;
    IUIAutomationCondition* andCond = nullptr;

    if (!type.empty()) {
        // 将 type 名称转回 ControlTypeId
        CONTROLTYPEID typeId = 0;
        if (type == "Button") typeId = UIA_ButtonControlTypeId;
        else if (type == "Edit") typeId = UIA_EditControlTypeId;
        else if (type == "CheckBox") typeId = UIA_CheckBoxControlTypeId;
        else if (type == "ComboBox") typeId = UIA_ComboBoxControlTypeId;
        else if (type == "MenuItem") typeId = UIA_MenuItemControlTypeId;
        else if (type == "Menu") typeId = UIA_MenuControlTypeId;
        else if (type == "MenuBar") typeId = UIA_MenuBarControlTypeId;
        else if (type == "Tab") typeId = UIA_TabControlTypeId;
        else if (type == "TabItem") typeId = UIA_TabItemControlTypeId;
        else if (type == "Text") typeId = UIA_TextControlTypeId;
        else if (type == "Tree") typeId = UIA_TreeControlTypeId;
        else if (type == "TreeItem") typeId = UIA_TreeItemControlTypeId;
        else if (type == "Window") typeId = UIA_WindowControlTypeId;
        else if (type == "Pane") typeId = UIA_PaneControlTypeId;
        else if (type == "ToolBar") typeId = UIA_ToolBarControlTypeId;
        else if (type == "List") typeId = UIA_ListControlTypeId;
        else if (type == "ListItem") typeId = UIA_ListItemControlTypeId;

        if (typeId != 0) {
            VARIANT varType;
            varType.vt = VT_I4;
            varType.lVal = typeId;
            g_pAutomation->CreatePropertyCondition(UIA_ControlTypePropertyId, varType, &typeCond);
            if (typeCond) {
                g_pAutomation->CreateAndCondition(nameCond, typeCond, &andCond);
                if (andCond) finalCond = andCond;
            }
        }
    }

    // 搜索
    IUIAutomationElement* found = nullptr;
    hr = root->FindFirst(TreeScope_Descendants, finalCond, &found);

    // 清理条件
    if (andCond) andCond->Release();
    if (typeCond) typeCond->Release();
    nameCond->Release();
    root->Release();

    if (FAILED(hr) || !found) {
        return "ERR element not found: " + name;
    }

    std::string result = "OK " + ElementInfoJson(found);
    found->Release();
    return result;
}

// 递归列出元素
static void ListElementsRecursive(IUIAutomationElement* parent, int depth, int maxDepth,
                                   std::string& result, int& count) {
    if (depth > maxDepth) return;

    IUIAutomationCondition* trueCond = nullptr;
    g_pAutomation->CreateTrueCondition(&trueCond);
    if (!trueCond) return;

    IUIAutomationElementArray* children = nullptr;
    HRESULT hr = parent->FindAll(TreeScope_Children, trueCond, &children);
    trueCond->Release();

    if (FAILED(hr) || !children) return;

    int length = 0;
    children->get_Length(&length);

    for (int i = 0; i < length && count < 200; i++) { // 限制最多200个元素
        IUIAutomationElement* child = nullptr;
        children->GetElement(i, &child);
        if (!child) continue;

        if (count > 0) result += ",";
        result += ElementInfoJson(child);
        count++;

        if (depth < maxDepth) {
            ListElementsRecursive(child, depth + 1, maxDepth, result, count);
        }

        child->Release();
    }

    children->Release();
}

std::string ListUIElements(HWND hwnd, int maxDepth) {
    if (!g_pAutomation) return "ERR UI Automation not initialized";

    IUIAutomationElement* root = nullptr;
    HRESULT hr = g_pAutomation->ElementFromHandle(hwnd, &root);
    if (FAILED(hr) || !root) return "ERR failed to get element from hwnd";

    std::string result = "OK [";
    int count = 0;
    ListElementsRecursive(root, 0, maxDepth, result, count);
    result += "]";

    root->Release();
    return result;
}

std::string ClickUIElement(const std::string& name, const std::string& type) {
    std::string findResult = FindUIElement(name, type);
    if (findResult.substr(0, 3) == "ERR") return findResult;

    if (!g_pAutomation) return "ERR UI Automation not initialized";

    // 重新查找元素来获取可点击位置
    HWND fgWnd = GetForegroundWindow();
    IUIAutomationElement* root = nullptr;
    g_pAutomation->ElementFromHandle(fgWnd, &root);
    if (!root) return "ERR no root element";

    int nameLen = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, NULL, 0);
    std::wstring wName(nameLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, &wName[0], nameLen);

    VARIANT varName;
    varName.vt = VT_BSTR;
    varName.bstrVal = SysAllocString(wName.c_str());
    IUIAutomationCondition* cond = nullptr;
    g_pAutomation->CreatePropertyCondition(UIA_NamePropertyId, varName, &cond);
    SysFreeString(varName.bstrVal);

    IUIAutomationElement* elem = nullptr;
    root->FindFirst(TreeScope_Descendants, cond, &elem);
    cond->Release();
    root->Release();

    if (!elem) return "ERR element not found for click: " + name;

    // 尝试 Invoke 模式
    IUIAutomationInvokePattern* invokePattern = nullptr;
    HRESULT hr = elem->GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern),
                                            (void**)&invokePattern);
    if (SUCCEEDED(hr) && invokePattern) {
        hr = invokePattern->Invoke();
        invokePattern->Release();
        if (SUCCEEDED(hr)) {
            RECT rect;
            elem->get_CurrentBoundingRectangle(&rect);
            int cx = (rect.left + rect.right) / 2;
            int cy = (rect.top + rect.bottom) / 2;
            elem->Release();
            return "OK {\"method\":\"invoke\",\"x\":" + std::to_string(cx)
                + ",\"y\":" + std::to_string(cy) + "}";
        }
    }

    // 回退：通过坐标点击
    RECT rect;
    elem->get_CurrentBoundingRectangle(&rect);
    elem->Release();

    int cx = (rect.left + rect.right) / 2;
    int cy = (rect.top + rect.bottom) / 2;

    SetCursorPos(cx, cy);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);

    return "OK {\"method\":\"click\",\"x\":" + std::to_string(cx)
        + ",\"y\":" + std::to_string(cy) + "}";
}
