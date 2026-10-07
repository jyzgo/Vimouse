#include "Clickables.h"
#include "State.h"
#include "Overlay.h"
#include "Screens.h"
#include "Input.h"
#include "History.h"
#include "Indicator.h"
#include "Util.h"
#include <objbase.h>
#include <uiautomation.h>
#include <dwmapi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <cwchar>

namespace {

// ================= 扫描（常驻 MTA 扫描线程） =================

using clk = std::chrono::steady_clock;

struct WinInfo { HWND hwnd; RECT rc; bool occluder; bool scan; };

bool GetFrame(HWND h, RECT& r) {
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(h, &r);
    return r.right > r.left && r.bottom > r.top;
}

bool IsCloaked(HWND h) {
    DWORD c = 0;
    return SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &c, sizeof(c))) && c != 0;
}

struct EnumCtx { std::vector<WinInfo>* out; DWORD selfPid; RECT area; };

BOOL CALLBACK EnumProc(HWND h, LPARAM lp) {
    auto* ctx = (EnumCtx*)lp;
    if (!IsWindowVisible(h) || IsIconic(h) || IsCloaked(h)) return TRUE;
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid == ctx->selfPid) return TRUE;
    LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
    if ((ex & WS_EX_LAYERED) && (ex & WS_EX_TRANSPARENT)) return TRUE;   // 点击穿透的叠加层
    if (ex & WS_EX_LAYERED) {
        BYTE a = 255; DWORD f = 0; COLORREF k = 0;
        if (GetLayeredWindowAttributes(h, &k, &a, &f) && (f & LWA_ALPHA) && a == 0) return TRUE;
    }
    RECT r, x;
    if (!GetFrame(h, r) || !IntersectRect(&x, &r, &ctx->area)) return TRUE;
    wchar_t cls[64] = {}; GetClassNameW(h, cls, 64);
    bool desktop = !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW");
    bool special = !wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd") || !wcscmp(cls, L"#32768");
    bool tool = (ex & WS_EX_TOOLWINDOW) != 0;
    WinInfo w{ h, r, !desktop && (!tool || special), !tool || special || desktop };
    ctx->out->push_back(w);
    return TRUE;
}

// 与 area 相交的顶层窗口，按 Z 序从上到下
std::vector<WinInfo> ListWindows(const RECT& area) {
    std::vector<WinInfo> wins;
    EnumCtx ctx{ &wins, GetCurrentProcessId(), area };
    EnumWindows(EnumProc, (LPARAM)&ctx);
    return wins;
}

bool Inside(POINT p, const RECT& r) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }
bool Contains(const RECT& outer, const RECT& in) { return in.left >= outer.left && in.top >= outer.top && in.right <= outer.right && in.bottom <= outer.bottom; }
bool NearSame(const RECT& a, const RECT& b) {
    return abs(a.left - b.left) <= 4 && abs(a.top - b.top) <= 4 && abs(a.right - b.right) <= 4 && abs(a.bottom - b.bottom) <= 4;
}

bool AddUnique(std::vector<RECT>& out, const RECT& r) {
    for (const RECT& o : out) if (NearSame(o, r)) return false;
    out.push_back(r);
    return true;
}

template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// 标准可点击控件
const CONTROLTYPEID kTypes[] = {
    UIA_ButtonControlTypeId, UIA_HyperlinkControlTypeId, UIA_MenuItemControlTypeId, UIA_ListItemControlTypeId,
    UIA_TabItemControlTypeId, UIA_CheckBoxControlTypeId, UIA_TreeItemControlTypeId, UIA_ComboBoxControlTypeId,
    UIA_EditControlTypeId, UIA_RadioButtonControlTypeId, UIA_SplitButtonControlTypeId, UIA_DataItemControlTypeId,
};
// 网页里用 div + 点击事件做的"按钮"（如 pty_share 的会话 tab）：类型是 Group/Text/Image/Custom，但支持 Invoke
const CONTROLTYPEID kGenericTypes[] = {
    UIA_GroupControlTypeId, UIA_TextControlTypeId, UIA_ImageControlTypeId, UIA_CustomControlTypeId,
};
constexpr int kGenericMaxW = 480, kGenericMaxH = 160;   // 超过这个尺寸的通用元素多半是带点击委托的整块面板

bool IsGenericType(CONTROLTYPEID t) {
    for (CONTROLTYPEID g : kGenericTypes) if (g == t) return true;
    return false;
}

IUIAutomationCondition* BoolCond(IUIAutomation* uia, PROPERTYID id, bool val) {
    VARIANT v; VariantInit(&v); v.vt = VT_BOOL; v.boolVal = val ? VARIANT_TRUE : VARIANT_FALSE;
    IUIAutomationCondition* c = nullptr;
    uia->CreatePropertyCondition(id, v, &c);
    return c;
}

// 组合条件；无论成败都释放输入
IUIAutomationCondition* Combine(IUIAutomation* uia, bool isAnd, std::vector<IUIAutomationCondition*> parts) {
    IUIAutomationCondition* c = nullptr;
    bool ok = !parts.empty() && std::all_of(parts.begin(), parts.end(), [](IUIAutomationCondition* p) { return p != nullptr; });
    if (ok) {
        if (isAnd) uia->CreateAndConditionFromNativeArray(parts.data(), (int)parts.size(), &c);
        else       uia->CreateOrConditionFromNativeArray(parts.data(), (int)parts.size(), &c);
    }
    for (auto* p : parts) if (p) p->Release();
    return c;
}

IUIAutomationCondition* TypesCond(IUIAutomation* uia, const CONTROLTYPEID* ids, size_t n) {
    std::vector<IUIAutomationCondition*> v;
    for (size_t i = 0; i < n; i++) {
        VARIANT x; VariantInit(&x); x.vt = VT_I4; x.lVal = ids[i];
        IUIAutomationCondition* c = nullptr;
        uia->CreatePropertyCondition(UIA_ControlTypePropertyId, x, &c);
        v.push_back(c);
    }
    return Combine(uia, false, std::move(v));
}

// 常驻扫描线程：UIA 对象和查询条件只建一次，所有扫描排队在这条 MTA 线程上跑
struct Scanner {
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::function<void()>> q;
    bool started = false;

    IUIAutomation* uia = nullptr;
    IUIAutomationCondition* cond = nullptr;
    IUIAutomationCacheRequest* cache = nullptr;
    std::string initErr;

    void Post(std::function<void()> job) {
        std::lock_guard<std::mutex> lk(m);
        if (!started) { started = true; std::thread([this] { Loop(); }).detach(); }
        q.push_back(std::move(job));
        cv.notify_one();
    }

    void Loop() {
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        Init();
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [this] { return !q.empty(); });
                job = std::move(q.front());
                q.pop_front();
            }
            job();
        }
    }

    void Init() {
        if (FAILED(CoCreateInstance(__uuidof(CUIAutomation8), NULL, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&uia)) &&
            FAILED(CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&uia))) {
            uia = nullptr;
            initErr = "CoCreateInstance(CUIAutomation) failed";
            return;
        }
        IUIAutomation2* u2 = nullptr;
        if (SUCCEEDED(uia->QueryInterface(__uuidof(IUIAutomation2), (void**)&u2))) {
            u2->put_ConnectionTimeout(800);
            u2->put_TransactionTimeout(1000);
            u2->Release();
        }
        // (标准控件 OR (通用类型 AND 支持 Invoke)) AND 不在屏幕外 AND 已启用
        IUIAutomationCondition* typed = TypesCond(uia, kTypes, _countof(kTypes));
        IUIAutomationCondition* generic = Combine(uia, true, {
            TypesCond(uia, kGenericTypes, _countof(kGenericTypes)),
            BoolCond(uia, UIA_IsInvokePatternAvailablePropertyId, true) });
        IUIAutomationCondition* any = Combine(uia, false, { typed, generic });
        cond = Combine(uia, true, {
            any,
            BoolCond(uia, UIA_IsOffscreenPropertyId, false),
            BoolCond(uia, UIA_IsEnabledPropertyId, true) });
        if (!cond) initErr = "CreateCondition failed";
        if (SUCCEEDED(uia->CreateCacheRequest(&cache))) {
            cache->AddProperty(UIA_BoundingRectanglePropertyId);
            cache->AddProperty(UIA_ControlTypePropertyId);
        } else {
            cache = nullptr;
            initErr = "CreateCacheRequest failed";
        }
    }

    std::vector<RECT> Scan(const RECT& area, std::string* report) {
        auto t0 = clk::now();
        std::vector<RECT> out;
        if (!uia || !cond || !cache) {
            if (report) *report = "ERR " + initErr;
            return out;
        }
        std::vector<WinInfo> wins = ListWindows(area);
        const long long areaPx = (long long)RectW(area) * RectH(area);
        std::string rep;
        int scanned = 0;
        for (size_t wi = 0; wi < wins.size(); wi++) {
            const WinInfo& w = wins[wi];
            if (!w.scan) continue;
            if (clk::now() - t0 > std::chrono::milliseconds(2500)) { rep += " [timeout]"; break; }
            RECT vis; IntersectRect(&vis, &w.rc, &area);
            bool hidden = false;   // 整个可见部分被上层某个窗口盖住 → 不扫
            for (size_t j = 0; j < wi && !hidden; j++) if (wins[j].occluder && Contains(wins[j].rc, vis)) hidden = true;
            if (hidden) continue;

            auto tw = clk::now();
            IUIAutomationElement* root = nullptr;
            IUIAutomationElementArray* found = nullptr;
            int added = 0, total = 0;
            if (SUCCEEDED(uia->ElementFromHandle(w.hwnd, &root)) && root &&
                SUCCEEDED(root->FindAllBuildCache(TreeScope_Descendants, cond, cache, &found)) && found) {
                found->get_Length(&total);
                std::vector<RECT> generic;
                for (int i = 0; i < total && out.size() < 3000; i++) {
                    IUIAutomationElement* e = nullptr;
                    if (FAILED(found->GetElement(i, &e)) || !e) continue;
                    RECT r = {};
                    CONTROLTYPEID ct = 0;
                    bool ok = SUCCEEDED(e->get_CachedBoundingRectangle(&r));
                    e->get_CachedControlType(&ct);
                    e->Release();
                    if (!ok) continue;
                    int rw = r.right - r.left, rh = r.bottom - r.top;
                    if (rw < 3 || rh < 3) continue;
                    if ((long long)rw * rh > areaPx / 4) continue;   // 整页/大面板，不算按钮
                    POINT c = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
                    if (!Inside(c, area) || !Inside(c, w.rc)) continue;
                    bool occluded = false;
                    for (size_t j = 0; j < wi && !occluded; j++) if (wins[j].occluder && Inside(c, wins[j].rc)) occluded = true;
                    if (occluded) continue;
                    if (IsGenericType(ct)) {
                        if (rw <= kGenericMaxW && rh <= kGenericMaxH) generic.push_back(r);
                        continue;
                    }
                    if (AddUnique(out, r)) added++;
                }
                // 通用可点元素：外层已经可点的就不再标内层（如 tab 里的计时徽章），落在标准控件里的也不标
                for (size_t i = 0; i < generic.size() && out.size() < 3000; i++) {
                    bool inner = false;
                    for (size_t j = 0; j < generic.size() && !inner; j++)
                        if (j != i && Contains(generic[j], generic[i]) && !NearSame(generic[j], generic[i])) inner = true;
                    for (size_t j = 0; j < out.size() && !inner; j++)
                        if (Contains(out[j], generic[i])) inner = true;
                    if (!inner && AddUnique(out, generic[i])) added++;
                }
            }
            SafeRelease(found);
            SafeRelease(root);
            scanned++;
            if (report && (added || total)) {
                wchar_t cls[64] = {}; GetClassNameW(w.hwnd, cls, 64);
                long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - tw).count();
                rep += " " + WideToUtf8(cls) + ":" + std::to_string(added) + "/" + std::to_string(total) + "@" + std::to_string(ms) + "ms";
            }
        }
        if (report) {
            long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t0).count();
            *report = "OK n=" + std::to_string(out.size()) + " ms=" + std::to_string(ms) +
                      " windows=" + std::to_string(scanned) + "/" + std::to_string(wins.size()) + rep;
        }
        return out;
    }
};

Scanner& GetScanner() {
    static Scanner* s = new Scanner;   // 线程常驻到进程退出，故意不析构
    return *s;
}

}  // namespace

std::vector<RECT> ScanClickables(const RECT& area, std::string* report) {
    auto p = std::make_shared<std::promise<std::vector<RECT>>>();
    auto rep = std::make_shared<std::string>();
    bool wantReport = report != nullptr;
    std::future<std::vector<RECT>> f = p->get_future();
    GetScanner().Post([p, rep, area, wantReport] { p->set_value(GetScanner().Scan(area, wantReport ? rep.get() : nullptr)); });
    std::vector<RECT> out = f.get();
    if (report) *report = *rep;
    return out;
}

// ================= 模式 / 叠加层（主线程） =================

namespace {

constexpr UINT WM_SCAN_DONE = WM_APP + 40;
constexpr UINT_PTR kExitTimer = 1;
constexpr COLORREF kKey = RGB(255, 0, 255);   // 透明色

// 标签底色：按首字母轮换，相邻标签 / 同一组两字母标签一眼可分
const COLORREF kPalette[] = {
    RGB(255, 220, 60), RGB(120, 215, 255), RGB(255, 160, 205), RGB(255, 170, 90), RGB(195, 170, 255), RGB(225, 225, 225),
};
const COLORREF kSelColor = RGB(0, 255, 160);

// 窗口签名：同一屏上窗口的句柄 / 位置 / 标题都没变，就先显示上次的扫描结果
struct WinSig { HWND h; RECT rc; std::wstring title; };

struct Target { RECT rc; std::string label; };
struct ScanResult { unsigned gen; RECT area; std::vector<WinSig> sig; std::vector<RECT> rects; };
struct ScanCache { bool valid = false; unsigned gen = 0; RECT area = {}; std::vector<WinSig> sig; std::vector<RECT> rects; };

HWND   g_wnd = NULL;
HFONT  g_font = NULL, g_msgFont = NULL;
std::vector<Target> g_targets;
int    g_sel = -1;
std::string g_prefix;
std::string g_alphabet;
bool   g_scanning = false;   // 扫描中且屏上还没有目标
unsigned g_gen = 0;
RECT   g_area = {};
std::wstring g_msg;
ScanCache g_cache;

POINT Center(const RECT& r) { return { (r.left + r.right) / 2, (r.top + r.bottom) / 2 }; }

std::vector<WinSig> Signature(const RECT& area) {
    std::vector<WinSig> sig;
    for (const WinInfo& w : ListWindows(area)) {
        if (!w.scan) continue;
        wchar_t title[256] = {};
        GetWindowTextW(w.hwnd, title, 256);
        sig.push_back({ w.hwnd, w.rc, title });
    }
    return sig;
}

bool SameSig(const std::vector<WinSig>& a, const std::vector<WinSig>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i].h != b[i].h || !EqualRect(&a[i].rc, &b[i].rc) || a[i].title != b[i].title) return false;
    return true;
}

void Redraw() { if (g_wnd) InvalidateRect(g_wnd, NULL, FALSE); }

void BuildAlphabet() {
    // 标签字母要避开本模式里有用的键（移动、左右键、本模式开关）
    const char* base = "ASEWRQTCVXZBIOUPMNY";
    std::vector<WORD> used;
    for (Action a : { Action::MoveLeft, Action::MoveDown, Action::MoveUp, Action::MoveRight,
                      Action::ClickLeft, Action::ClickRight, Action::ClickMode })
        if (!g_keymap[(int)a].ctrl && !g_keymap[(int)a].alt) used.push_back(g_keymap[(int)a].vk);
    g_alphabet.clear();
    for (const char* p = base; *p; p++)
        if (std::find(used.begin(), used.end(), (WORD)*p) == used.end()) g_alphabet += *p;
    if (g_alphabet.size() < 2) g_alphabet = "ASEWRQTCVXZ";
}

std::vector<Target> BuildTargets(std::vector<RECT> rects) {
    const size_t L = g_alphabet.size();
    const size_t cap = L * L;
    if (rects.size() > cap) {
        POINT p; GetCursorPos(&p);
        auto d2 = [&p](const RECT& r) { POINT c = Center(r); long long dx = c.x - p.x, dy = c.y - p.y; return dx * dx + dy * dy; };
        std::nth_element(rects.begin(), rects.begin() + cap, rects.end(), [&](const RECT& a, const RECT& b) { return d2(a) < d2(b); });
        rects.resize(cap);
    }
    // 阅读顺序（按 24px 行分桶，再从左到右）
    std::sort(rects.begin(), rects.end(), [](const RECT& a, const RECT& b) {
        int ra = Center(a).y / 24, rb = Center(b).y / 24;
        return ra != rb ? ra < rb : a.left < b.left;
    });
    std::vector<Target> out;
    for (size_t i = 0; i < rects.size(); i++) {
        Target t; t.rc = rects[i];
        if (rects.size() <= L) t.label = std::string(1, g_alphabet[i]);
        else t.label = std::string{ g_alphabet[i / L], g_alphabet[i % L] };
        out.push_back(t);
    }
    return out;
}

bool SameTargets(const std::vector<Target>& a, const std::vector<Target>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i].label != b[i].label || !NearSame(a[i].rc, b[i].rc)) return false;
    return true;
}

void LaunchScan(std::vector<WinSig> sig) {
    unsigned gen = ++g_gen;
    RECT area = g_area;
    HWND hwnd = g_wnd;
    auto sp = std::make_shared<std::vector<WinSig>>(std::move(sig));
    GetScanner().Post([gen, area, hwnd, sp] {
        auto* r = new ScanResult{ gen, area, std::move(*sp), GetScanner().Scan(area, nullptr) };
        if (!PostMessageW(hwnd, WM_SCAN_DONE, 0, (LPARAM)r)) delete r;
    });
}

// 清空并重新扫描（屏上显示"正在扫描"）
void StartScan() {
    g_scanning = true;
    g_targets.clear();
    g_sel = -1;
    g_prefix.clear();
    g_msg = IsSystemChinese() ? L"正在扫描可点击元素…" : L"Scanning clickable elements…";
    LaunchScan(Signature(g_area));
    Redraw();
}

void SelectTarget(int i) {
    if (i < 0 || i >= (int)g_targets.size()) return;
    g_sel = i;
    POINT c = Center(g_targets[i].rc);
    SetCursorPos(c.x, c.y);
    Redraw();
}

int FindInDirection(unsigned bit) {
    POINT o; RECT cur;
    if (g_sel >= 0) { cur = g_targets[g_sel].rc; o = Center(cur); }
    else { GetCursorPos(&o); cur = { o.x, o.y, o.x + 1, o.y + 1 }; }
    int best = -1; double bestScore = 1e18;
    for (int i = 0; i < (int)g_targets.size(); i++) {
        if (i == g_sel) continue;
        const RECT& r = g_targets[i].rc;
        POINT c = Center(r);
        double primary, secondary; bool overlap;
        switch (bit) {
        case MV_RIGHT: primary = c.x - o.x; secondary = abs(c.y - o.y); overlap = r.top < cur.bottom && r.bottom > cur.top; break;
        case MV_LEFT:  primary = o.x - c.x; secondary = abs(c.y - o.y); overlap = r.top < cur.bottom && r.bottom > cur.top; break;
        case MV_DOWN:  primary = c.y - o.y; secondary = abs(c.x - o.x); overlap = r.left < cur.right && r.right > cur.left; break;
        case MV_UP:    primary = o.y - c.y; secondary = abs(c.x - o.x); overlap = r.left < cur.right && r.right > cur.left; break;
        default: return -1;
        }
        if (primary <= 2) continue;
        double score = primary + secondary * (overlap ? 0.3 : 2.0);
        if (score < bestScore) { bestScore = score; best = i; }
    }
    return best;
}

void TypeLetter(char ch) {
    if (g_targets.empty()) return;
    std::string want = g_prefix + ch;
    bool anyPrefix = false;
    for (int i = 0; i < (int)g_targets.size(); i++) {
        const std::string& lb = g_targets[i].label;
        if (lb == want) { g_prefix.clear(); SelectTarget(i); return; }
        if (lb.compare(0, want.size(), want) == 0) anyPrefix = true;
    }
    g_prefix = anyPrefix ? want : std::string();
    Redraw();
}

void DoClick(Button b) {
    ExitClickMode();
    Click(b);
    TriggerClickFlash(true);
    if (b == Button::Left) AddMousePositionToStack();
}

int PaletteIndex(const Target& t) {
    size_t k = g_alphabet.find(t.label[0]);
    return (int)((k == std::string::npos ? 0 : k) % _countof(kPalette));
}

void DrawPill(HDC dc, const RECT& client, const std::wstring& text) {
    HGDIOBJ old = SelectObject(dc, g_msgFont);
    SIZE sz; GetTextExtentPoint32W(dc, text.c_str(), (int)text.size(), &sz);
    int w = sz.cx + 40, h = sz.cy + 20;
    RECT r = { (client.right - w) / 2, (client.bottom - h) / 2, (client.right + w) / 2, (client.bottom + h) / 2 };
    HBRUSH bg = CreateSolidBrush(RGB(20, 20, 30));
    FillRect(dc, &r, bg);
    DeleteObject(bg);
    SetTextColor(dc, RGB(255, 230, 120));
    DrawTextW(dc, text.c_str(), (int)text.size(), &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old);
}

void Paint(HWND hwnd) {
    DoubleBuffer db(hwnd);
    HBRUSH key = CreateSolidBrush(kKey);
    FillRect(db.mem, &db.rc, key);
    DeleteObject(key);
    if (!g_font) g_font = MakeFont(18, FW_BOLD, L"Arial");
    if (!g_msgFont) g_msgFont = MakeFont(24, FW_BOLD, L"Microsoft YaHei UI");

    const int N = (int)_countof(kPalette);
    HPEN pens[_countof(kPalette)];
    HBRUSH brushes[_countof(kPalette)];
    for (int i = 0; i < N; i++) { pens[i] = CreatePen(PS_SOLID, 2, kPalette[i]); brushes[i] = CreateSolidBrush(kPalette[i]); }
    HPEN selPen = CreatePen(PS_SOLID, 3, kSelColor);
    HBRUSH selBg = CreateSolidBrush(kSelColor);
    HBRUSH border = CreateSolidBrush(RGB(0, 0, 0));

    HGDIOBJ oldPen = SelectObject(db.mem, pens[0]);
    HGDIOBJ oldBrush = SelectObject(db.mem, GetStockObject(NULL_BRUSH));
    for (int i = 0; i < (int)g_targets.size(); i++) {
        const Target& t = g_targets[i];
        if (!g_prefix.empty() && t.label.compare(0, g_prefix.size(), g_prefix) != 0) continue;
        RECT r = t.rc;
        OffsetRect(&r, -g_area.left, -g_area.top);
        SelectObject(db.mem, i == g_sel ? selPen : pens[PaletteIndex(t)]);
        Rectangle(db.mem, r.left, r.top, r.right, r.bottom);
    }
    SelectObject(db.mem, oldBrush);
    SelectObject(db.mem, oldPen);

    HGDIOBJ oldFont = SelectObject(db.mem, g_font);
    SetBkMode(db.mem, TRANSPARENT);
    for (int i = 0; i < (int)g_targets.size(); i++) {
        const Target& t = g_targets[i];
        if (!g_prefix.empty() && t.label.compare(0, g_prefix.size(), g_prefix) != 0) continue;
        RECT r = t.rc;
        OffsetRect(&r, -g_area.left, -g_area.top);
        SIZE sz; GetTextExtentPoint32A(db.mem, t.label.c_str(), (int)t.label.size(), &sz);
        int w = sz.cx + 8, h = sz.cy + 2;
        int x = max(0, min((int)db.rc.right - w, (int)r.left - 2));
        int y = max(0, min((int)db.rc.bottom - h, (int)r.top - 2));
        RECT box = { x, y, x + w, y + h };
        FillRect(db.mem, &box, i == g_sel ? selBg : brushes[PaletteIndex(t)]);
        FrameRect(db.mem, &box, border);
        int tx = x + 4;
        for (size_t k = 0; k < t.label.size(); k++) {
            // 已输入的字母变灰；第一个字母黑、第二个字母深红，两字母标签的边界一眼看清
            bool typed = k < g_prefix.size();
            SetTextColor(db.mem, typed ? RGB(120, 120, 120) : (k == 0 ? RGB(0, 0, 0) : RGB(170, 0, 0)));
            TextOutA(db.mem, tx, y + 1, &t.label[k], 1);
            SIZE cs; GetTextExtentPoint32A(db.mem, &t.label[k], 1, &cs);
            tx += cs.cx;
        }
    }
    SelectObject(db.mem, oldFont);

    for (int i = 0; i < N; i++) { DeleteObject(pens[i]); DeleteObject(brushes[i]); }
    DeleteObject(selPen);
    DeleteObject(selBg);
    DeleteObject(border);

    if (!g_msg.empty()) DrawPill(db.mem, db.rc, g_msg);
}

void OnScanDone(HWND hwnd, ScanResult* r) {
    // 不管模式还在不在，较新的结果都写进缓存（用户可能已用缓存目标点完退出了）
    if (!g_cache.valid || (int)(r->gen - g_cache.gen) > 0) {
        g_cache.valid = true;
        g_cache.gen = r->gen;
        g_cache.area = r->area;
        g_cache.sig = r->sig;
        g_cache.rects = r->rects;
    }
    if (r->gen != g_gen || !g_clickMode) return;
    std::vector<Target> fresh = BuildTargets(std::move(r->rects));
    if (g_scanning) {
        g_scanning = false;
        g_targets = std::move(fresh);
    } else if (g_prefix.empty() && g_sel < 0 && !SameTargets(g_targets, fresh)) {
        g_targets = std::move(fresh);   // 屏上是缓存结果且用户还没开始选：换成最新结果
    } else {
        return;                         // 结果没变，或用户已经在选了：不打扰
    }
    if (g_targets.empty()) {
        g_msg = IsSystemChinese() ? L"这个屏幕上没找到可点击元素" : L"No clickable elements found";
        SetTimer(hwnd, kExitTimer, 1200, NULL);
    } else {
        g_msg.clear();
    }
    Redraw();
}

LRESULT CALLBACK ClickWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SCAN_DONE: {
        ScanResult* r = (ScanResult*)lParam;
        OnScanDone(hwnd, r);
        delete r;
        return 0;
    }
    case WM_TIMER:
        if (wParam == kExitTimer) { KillTimer(hwnd, kExitTimer); if (g_clickMode && g_targets.empty() && !g_scanning) ExitClickMode(); }
        return 0;
    case WM_ERASEBKGND:
        return 1;   // 整窗都由 WM_PAINT 双缓冲画；不擦背景，否则每次重画先刷一遍黑底 → 闪
    case WM_PAINT:
        Paint(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_font) { DeleteObject(g_font); g_font = NULL; }
        if (g_msgFont) { DeleteObject(g_msgFont); g_msgFont = NULL; }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool IsModVk(DWORD vk) {
    switch (vk) {
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: case VK_LWIN: case VK_RWIN: return true;
    }
    return false;
}

}  // namespace

void CreateClickWindow() {
    g_wnd = CreateOverlayWindow(L"VimouseClickables", ClickWndProc,
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, 0, 0, 0, 0, 255);
    if (g_wnd) SetLayeredWindowAttributes(g_wnd, kKey, 235, LWA_COLORKEY | LWA_ALPHA);
    GetScanner().Post([] {});   // 提前起扫描线程、建好 UIA 对象，第一次进模式也省掉这段
}

void DestroyClickWindow() {
    if (g_wnd) { DestroyWindow(g_wnd); g_wnd = NULL; }
}

void EnterClickMode() {
    if (!g_wnd) return;
    if (g_clickMode) { StartScan(); return; }   // 再按一次 = 重新扫描
    g_clickMode = true;
    BuildAlphabet();
    RefreshScreens();
    g_area = ScreenRectAt(GetCurrentScreenIndex());
    KillTimer(g_wnd, kExitTimer);
    g_sel = -1;
    g_prefix.clear();
    std::vector<WinSig> sig = Signature(g_area);
    if (g_cache.valid && !g_cache.rects.empty() && EqualRect(&g_cache.area, &g_area) && SameSig(g_cache.sig, sig)) {
        // 窗口布局没变：立即显示上次结果，后台再扫一遍校正
        g_scanning = false;
        g_msg.clear();
        g_targets = BuildTargets(g_cache.rects);
        LaunchScan(std::move(sig));
    } else {
        g_scanning = true;
        g_targets.clear();
        g_msg = IsSystemChinese() ? L"正在扫描可点击元素…" : L"Scanning clickable elements…";
        LaunchScan(std::move(sig));
    }
    SetWindowPos(g_wnd, HWND_TOPMOST, g_area.left, g_area.top, RectW(g_area), RectH(g_area), SWP_NOACTIVATE | SWP_SHOWWINDOW);
    Redraw();
    UpdateIndicatorPosition();
}

void ExitClickMode() {
    if (!g_clickMode) return;
    g_clickMode = false;
    g_gen++;
    g_scanning = false;
    g_targets.clear();
    g_sel = -1;
    g_prefix.clear();
    g_msg.clear();
    if (g_wnd) { KillTimer(g_wnd, kExitTimer); ShowWindow(g_wnd, SW_HIDE); }
    UpdateIndicatorPosition();
}

int ClickModeStatus() { return g_scanning ? -1 : (int)g_targets.size(); }

bool HandleClickKeyDown(DWORD vk, Modifiers m) {
    if (vk == VK_ESCAPE) {
        if (!g_prefix.empty()) { g_prefix.clear(); Redraw(); }
        else ExitClickMode();
        return true;
    }
    if (IsModVk(vk)) return false;
    if (IsAction(Action::ClickMode, (WORD)vk, m)) { StartScan(); return true; }
    if (IsAction(Action::ClickLeft, (WORD)vk, m))  { DoClick(Button::Left); return true; }
    if (IsAction(Action::ClickRight, (WORD)vk, m)) { DoClick(Button::Right); return true; }
    if (g_scanning) return true;
    if (vk == VK_BACK) { g_prefix.clear(); Redraw(); return true; }
    unsigned bit = (vk == VK_LEFT) ? MV_LEFT : (vk == VK_RIGHT) ? MV_RIGHT : (vk == VK_UP) ? MV_UP : (vk == VK_DOWN) ? MV_DOWN : 0;
    if (!bit) {
        unsigned mb = MoveBitOf((WORD)vk, m) & (MV_LEFT | MV_DOWN | MV_UP | MV_RIGHT);
        for (unsigned b : { MV_LEFT, MV_DOWN, MV_UP, MV_RIGHT }) if (mb & b) { bit = b; break; }
    }
    if (bit) {
        g_prefix.clear();
        int i = FindInDirection(bit);
        if (i >= 0) SelectTarget(i); else Redraw();
        return true;
    }
    if (vk >= 'A' && vk <= 'Z' && !m.ctrl && !m.alt && g_alphabet.find((char)vk) != std::string::npos) {
        TypeLetter((char)vk);
        return true;
    }
    return true;   // 其他键吞掉，避免误输入
}
