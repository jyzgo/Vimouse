#include "Clickables.h"
#include "State.h"
#include "Overlay.h"
#include "Screens.h"
#include "Input.h"
#include "History.h"
#include "Indicator.h"
#include "Hint.h"
#include "Util.h"
#include <objbase.h>
#include <uiautomation.h>
#include <dwmapi.h>
#include <algorithm>
#include <atomic>
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

// 扫描日志：%TEMP%\vimouse_scan.log，每次扫描一行（时间、总耗时、各窗口 类名:命中/总数@定位+查找耗时）。
// 用来排查"有时快有时慢"到底卡在哪个窗口；超过 256KB 就清空重写。
void AppendScanLog(const std::string& line) {
    wchar_t dir[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, dir)) return;
    std::wstring path = std::wstring(dir) + L"vimouse_scan.log";
    WIN32_FILE_ATTRIBUTE_DATA fa = {};
    bool big = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) && (fa.nFileSizeHigh || fa.nFileSizeLow > 256 * 1024);
    HANDLE f = CreateFileW(path.c_str(), big ? GENERIC_WRITE : FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                           big ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st; GetLocalTime(&st);
    char ts[32];
    sprintf_s(ts, "%02d-%02d %02d:%02d:%02d.%03d ", st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    std::string s = ts + line + "\r\n";
    DWORD n = 0;
    WriteFile(f, s.data(), (DWORD)s.size(), &n, NULL);
    CloseHandle(f);
}

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
// 例外：又宽又矮的整行（pty_share 的 "… +N lines (点击展开)" 566×23、底部 cx:… HUD 条 530×23）是真按钮
constexpr int kGenericRowMaxW = 1400, kGenericRowMaxH = 48;

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

// 通用可点元素并入标准控件：外层已经可点的就不再标内层（如 tab 里的计时徽章），落在标准控件里的也不标
std::vector<RECT> MergeGeneric(std::vector<RECT> out, const std::vector<RECT>& generic) {
    const size_t nStd = out.size();
    for (size_t i = 0; i < generic.size() && out.size() < 3000; i++) {
        bool inner = false;
        for (size_t j = 0; j < generic.size() && !inner; j++)
            if (j != i && Contains(generic[j], generic[i]) && !NearSame(generic[j], generic[i])) inner = true;
        for (size_t j = 0; j < nStd && !inner; j++)
            if (Contains(out[j], generic[i])) inner = true;
        if (!inner) AddUnique(out, generic[i]);
    }
    return out;
}

// 常驻扫描线程：UIA 对象和查询条件只建一次，扫描排队在这条 MTA 线程上跑。
// 两个实例：标准控件一条、通用可点元素一条 —— 后者在某些网页上一次能匹配上万个节点（实测 25s），
// 分开后它再慢也挡不住标准控件先显示，也不会堵住下一次扫描的标准控件阶段
struct Scanner {
    explicit Scanner(bool g) : genericPhase(g) {}
    const bool genericPhase;
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
        auto hx = [](HRESULT hr) { char b[16]; sprintf_s(b, "%08lX", (unsigned long)hr); return std::string(b); };
        SafeRelease(cache);
        SafeRelease(cond);
        SafeRelease(uia);
        initErr.clear();
        HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation8), NULL, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&uia);
        if (FAILED(hr) || !uia) hr = CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&uia);
        if (FAILED(hr) || !uia) {
            uia = nullptr;
            initErr = "CoCreateInstance(CUIAutomation) failed hr=" + hx(hr);
            return;
        }
        IUIAutomation2* u2 = nullptr;
        if (SUCCEEDED(uia->QueryInterface(__uuidof(IUIAutomation2), (void**)&u2))) {
            u2->put_ConnectionTimeout(800);
            u2->put_TransactionTimeout(1000);
            u2->Release();
        }
        if (genericPhase)   // 通用类型（Group/Text/Image/Custom）且支持 Invoke：网页里的自定义可点 div
            cond = Combine(uia, true, {
                TypesCond(uia, kGenericTypes, _countof(kGenericTypes)),
                BoolCond(uia, UIA_IsInvokePatternAvailablePropertyId, true),
                BoolCond(uia, UIA_IsOffscreenPropertyId, false),
                BoolCond(uia, UIA_IsEnabledPropertyId, true) });
        else                // 标准可点控件
            cond = Combine(uia, true, {
                TypesCond(uia, kTypes, _countof(kTypes)),
                BoolCond(uia, UIA_IsOffscreenPropertyId, false),
                BoolCond(uia, UIA_IsEnabledPropertyId, true) });
        if (!cond) initErr = "CreateCondition failed";
        hr = uia->CreateCacheRequest(&cache);
        if (SUCCEEDED(hr) && cache) {
            cache->AddProperty(UIA_BoundingRectanglePropertyId);
            cache->AddProperty(UIA_ControlTypePropertyId);
        } else {
            cache = nullptr;
            initErr += " CreateCacheRequest failed hr=" + hx(hr);
        }
    }

    // 标准实例：去重后的标准控件；通用实例：尺寸过滤后的原始通用元素（之后用 MergeGeneric 并入）
    std::vector<RECT> Scan(const RECT& area, std::string* report) {
        auto t0 = clk::now();
        std::vector<RECT> out;
        if (!uia || !cond || !cache) Init();   // 初始化失败过：每次扫描前重试
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
            HRESULT hrRoot = uia->ElementFromHandle(w.hwnd, &root);
            auto tr = clk::now();
            HRESULT hrFind = E_FAIL;
            if (SUCCEEDED(hrRoot) && root) hrFind = root->FindAllBuildCache(TreeScope_Descendants, cond, cache, &found);
            auto tf = clk::now();
            if (SUCCEEDED(hrFind) && found) {
                found->get_Length(&total);
                std::vector<std::pair<CONTROLTYPEID, int>> hist;   // 匹配数异常多时记录类型分布
                for (int i = 0; i < total && out.size() < 3000; i++) {
                    IUIAutomationElement* e = nullptr;
                    if (FAILED(found->GetElement(i, &e)) || !e) continue;
                    RECT r = {};
                    CONTROLTYPEID ct = 0;
                    bool ok = SUCCEEDED(e->get_CachedBoundingRectangle(&r));
                    e->get_CachedControlType(&ct);
                    e->Release();
                    if (total > 500) {
                        auto it = std::find_if(hist.begin(), hist.end(), [ct](const std::pair<CONTROLTYPEID, int>& p) { return p.first == ct; });
                        if (it == hist.end()) hist.push_back({ ct, 1 }); else it->second++;
                    }
                    if (!ok) continue;
                    int rw = r.right - r.left, rh = r.bottom - r.top;
                    if (rw < 3 || rh < 3) continue;
                    if ((long long)rw * rh > areaPx / 4) continue;   // 整页/大面板，不算按钮
                    POINT c = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
                    if (!Inside(c, area) || !Inside(c, w.rc)) continue;
                    bool occluded = false;
                    for (size_t j = 0; j < wi && !occluded; j++) if (wins[j].occluder && Inside(c, wins[j].rc)) occluded = true;
                    if (occluded) continue;
                    if (genericPhase) {
                        if ((rw <= kGenericMaxW && rh <= kGenericMaxH) || (rw <= kGenericRowMaxW && rh <= kGenericRowMaxH)) {
                            out.push_back(r); added++;
                        }
                        continue;
                    }
                    if (AddUnique(out, r)) added++;
                }
                if (!hist.empty()) {
                    std::sort(hist.begin(), hist.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
                    rep += "{";
                    for (size_t k = 0; k < hist.size() && k < 4; k++)
                        rep += (k ? "," : "") + std::to_string(hist[k].first) + "x" + std::to_string(hist[k].second);
                    rep += "}";
                }
            }
            SafeRelease(found);
            SafeRelease(root);
            scanned++;
            {
                auto msOf = [](clk::duration d) { return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(d).count(); };
                long long ms = msOf(clk::now() - tw);
                if (added || total || ms >= 100 || FAILED(hrRoot) || FAILED(hrFind)) {
                    wchar_t cls[64] = {}; GetClassNameW(w.hwnd, cls, 64);
                    rep += " " + WideToUtf8(cls) + ":" + std::to_string(added) + "/" + std::to_string(total) + "@" + std::to_string(ms) + "ms";
                    if (ms >= 100) rep += "(root " + std::to_string(msOf(tr - tw)) + " find " + std::to_string(msOf(tf - tr)) + ")";
                    if (FAILED(hrRoot) || FAILED(hrFind)) {
                        char hx[16]; sprintf_s(hx, "%08lX", (unsigned long)(FAILED(hrRoot) ? hrRoot : hrFind));
                        rep += std::string("[hr=") + hx + "]";
                    }
                }
            }
        }
        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t0).count();
        std::string summary = std::string(genericPhase ? "gen" : "std") + " n=" + std::to_string(out.size()) + " ms=" + std::to_string(ms) +
                              " windows=" + std::to_string(scanned) + "/" + std::to_string(wins.size()) + rep;
        AppendScanLog(summary);
        if (report) *report = summary;
        return out;
    }
};

Scanner& GetScanner(bool generic) {
    static Scanner* s = new Scanner(false);   // 线程常驻到进程退出，故意不析构
    static Scanner* g = new Scanner(true);
    return generic ? *g : *s;
}

// 只有最新一次扫描值得跑：排队期间又发起了新扫描，旧的直接跳过（尤其是偶尔很慢的通用实例）
std::atomic<unsigned> g_latestJob{ 0 };

}  // namespace

std::vector<RECT> ScanClickables(const RECT& area, std::string* report) {
    auto t0 = clk::now();
    std::future<std::vector<RECT>> f[2];
    auto rep = std::make_shared<std::vector<std::string>>(2);
    for (int i = 0; i < 2; i++) {
        auto p = std::make_shared<std::promise<std::vector<RECT>>>();
        f[i] = p->get_future();
        GetScanner(i == 1).Post([p, rep, area, i] { p->set_value(GetScanner(i == 1).Scan(area, &(*rep)[i])); });
    }
    std::vector<RECT> std_ = f[0].get();
    long long msStd = std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t0).count();
    std::vector<RECT> out = MergeGeneric(std::move(std_), f[1].get());
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(clk::now() - t0).count();
    if (report) *report = "OK n=" + std::to_string(out.size()) + " ms=" + std::to_string(ms) + " first=" + std::to_string(msStd) +
                          "ms | " + (*rep)[0] + " | " + (*rep)[1];
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
struct ScanResult { unsigned gen; bool generic; RECT area; std::vector<WinSig> sig; std::vector<RECT> rects; };
struct ScanCache { bool valid = false; unsigned gen = 0; RECT area = {}; std::vector<WinSig> sig; std::vector<RECT> rects; };
// 当前这次扫描的两半结果（标准控件 / 通用可点元素各自一条线程，先到先用）
struct PendingScan { unsigned gen = 0; bool haveStd = false, haveGen = false; std::vector<RECT> std_, gen_; };

HWND   g_wnd = NULL;
HFONT  g_font = NULL, g_msgFont = NULL;
std::vector<Target> g_targets;
int    g_sel = -1;
// hjkl 走过的路：{出发的框, 方向}。按反方向时原路退回，保证 h 再 l（或 l 再 h）一定回到原处
std::vector<std::pair<int, unsigned>> g_moveTrail;
std::string g_prefix;
bool   g_scanning = false;   // 扫描中且屏上还没有目标
unsigned g_gen = 0;
RECT   g_area = {};
std::wstring g_msg;
ScanCache g_cache;
PendingScan g_pend;

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

void Render();
void Redraw() { Render(); }

// ---- Hint 同款坐标：屏幕 26×26 格，标签 = 列字母 + 行字母 ----
constexpr int kGrid = 26;

long long Dist2(POINT a, POINT b) { long long dx = a.x - b.x, dy = a.y - b.y; return dx * dx + dy * dy; }

// 点所在的 Hint 格（越界夹到边上）
void CellOf(POINT p, int& col, int& row) {
    int w = max(1, (int)RectW(g_area)), h = max(1, (int)RectH(g_area));
    col = max(0, min(kGrid - 1, (int)((long long)(p.x - g_area.left) * kGrid / w)));
    row = max(0, min(kGrid - 1, (int)((long long)(p.y - g_area.top) * kGrid / h)));
}

std::string CellLabel(int col, int row) { return std::string{ (char)('A' + col), (char)('A' + row) }; }

// 坐标第一个字母（列）不能用本模式的命令键：移动 hjkl、左/右键 f/g、关闭 d —— 否则按 h 分不清是"往左跳"还是"输坐标"。
// 第二个字母（行）已经在输坐标的状态里，26 个随便用，跟 Hint 完全一致。
bool g_colBanned[kGrid] = {};

void BuildColumnBans() {
    for (bool& b : g_colBanned) b = false;
    for (Action a : { Action::MoveLeft, Action::MoveDown, Action::MoveUp, Action::MoveRight,
                      Action::ClickLeft, Action::ClickRight, Action::ClickMode }) {
        const KeyChord& k = g_keymap[(int)a];
        if (!k.ctrl && !k.alt && k.vk >= 'A' && k.vk <= 'Z') g_colBanned[k.vk - 'A'] = true;
    }
}

bool IsCommandLetter(char ch) { return ch >= 'A' && ch <= 'Z' && g_colBanned[ch - 'A']; }

// 每个框标上它中心所在的 Hint 格坐标 —— 跟 Hint 模式里同一位置敲的字母一样，形成肌肉记忆。
// 列落在命令键字母上、或同格已被占：挪到离框中心最近的可用格（列换成相邻的可用字母，行尽量不变）。
// 可用格用完的丢掉离光标最远的
std::vector<Target> BuildTargets(std::vector<RECT> rects) {
    int freeCols = 0;
    for (bool b : g_colBanned) if (!b) freeCols++;
    const size_t cap = (size_t)freeCols * kGrid;
    if (rects.size() > cap) {
        POINT p; GetCursorPos(&p);
        std::nth_element(rects.begin(), rects.begin() + cap, rects.end(),
                         [&](const RECT& a, const RECT& b) { return Dist2(Center(a), p) < Dist2(Center(b), p); });
        rects.resize(cap);
    }
    struct Item { RECT rc; POINT c; int col, row; long long d; };
    std::vector<Item> items;
    for (const RECT& r : rects) {
        Item it{ r, Center(r) };
        CellOf(it.c, it.col, it.row);
        it.d = Dist2(it.c, HintCellCenter(g_area, it.col, it.row));
        items.push_back(it);
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.d < b.d; });
    bool used[kGrid][kGrid] = {};
    for (int c = 0; c < kGrid; c++) if (g_colBanned[c]) for (int r = 0; r < kGrid; r++) used[c][r] = true;
    std::vector<Target> out;
    for (const Item& it : items) {
        int col = it.col, row = it.row;
        if (used[col][row]) {
            long long best = -1;
            for (int c = 0; c < kGrid; c++)
                for (int r = 0; r < kGrid; r++) {
                    if (used[c][r]) continue;
                    long long d = Dist2(it.c, HintCellCenter(g_area, c, r));
                    if (best < 0 || d < best) { best = d; col = c; row = r; }
                }
        }
        used[col][row] = true;
        out.push_back({ it.rc, CellLabel(col, row) });
    }
    // 稳定顺序（按标签），SameTargets 才能判"结果没变"
    std::sort(out.begin(), out.end(), [](const Target& a, const Target& b) { return a.label < b.label; });
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
    g_latestJob = gen;
    g_pend = PendingScan{};
    g_pend.gen = gen;
    RECT area = g_area;
    HWND hwnd = g_wnd;
    auto sp = std::make_shared<std::vector<WinSig>>(std::move(sig));
    for (bool generic : { false, true }) {
        GetScanner(generic).Post([gen, generic, area, hwnd, sp] {
            if (g_latestJob != gen) return;   // 已经有更新的扫描排在后面
            auto* r = new ScanResult{ gen, generic, area, *sp, GetScanner(generic).Scan(area, nullptr) };
            if (!PostMessageW(hwnd, WM_SCAN_DONE, 0, (LPARAM)r)) delete r;
        });
    }
}

void SelectTarget(int i) {
    if (i < 0 || i >= (int)g_targets.size()) return;
    g_sel = i;
    POINT c = Center(g_targets[i].rc);
    SetCursorPos(c.x, c.y);
    Redraw();
}

// 两段区间 [a0,a1) 与 [b0,b1) 之间的空隙；重叠 = 0
static long Gap(long a0, long a1, long b0, long b1) { return max(0L, max(b0 - a1, a0 - b1)); }

// 按「边到边」距离找：宽框（如整行的 +N lines）中心离得远但边紧挨着，也该优先。
// primary = 该方向上两框之间的空隙，perp = 垂直方向的空隙（同一行/列内为 0）；
// 垂直偏离罚得重（×2.5），中心距离只做轻微的平局裁决
int FindInDirection(unsigned bit) {
    POINT o; RECT cur;
    if (g_sel >= 0) { cur = g_targets[g_sel].rc; o = Center(cur); }
    else { GetCursorPos(&o); cur = { o.x, o.y, o.x + 1, o.y + 1 }; }
    int best = -1; double bestScore = 1e18;
    for (int i = 0; i < (int)g_targets.size(); i++) {
        if (i == g_sel) continue;
        const RECT& r = g_targets[i].rc;
        POINT c = Center(r);
        double ahead, primary, perp, centerOff;
        switch (bit) {
        case MV_RIGHT: ahead = c.x - o.x; primary = max(0L, r.left - cur.right);  perp = Gap(cur.top, cur.bottom, r.top, r.bottom); centerOff = abs(c.y - o.y); break;
        case MV_LEFT:  ahead = o.x - c.x; primary = max(0L, cur.left - r.right);  perp = Gap(cur.top, cur.bottom, r.top, r.bottom); centerOff = abs(c.y - o.y); break;
        case MV_DOWN:  ahead = c.y - o.y; primary = max(0L, r.top - cur.bottom);  perp = Gap(cur.left, cur.right, r.left, r.right); centerOff = abs(c.x - o.x); break;
        case MV_UP:    ahead = o.y - c.y; primary = max(0L, cur.top - r.bottom);  perp = Gap(cur.left, cur.right, r.left, r.right); centerOff = abs(c.x - o.x); break;
        default: return -1;
        }
        if (ahead <= 2) continue;   // 中心必须确实在那个方向
        double score = primary + perp * 2.5 + (ahead + centerOff) * 0.05;
        if (score < bestScore) { bestScore = score; best = i; }
    }
    return best;
}

// 跟 Hint 一样两字母一组：第一个是列，第二个是行。
// 正好有框标着这个坐标就选它；没有（那格是空的）就选离那格中心最近的框 —— 照 Hint 习惯敲也能落到附近的按钮。
// 选中后停在本模式：接着可以 hjkl 微调、f 点击，或者再敲一组坐标
void TypeLetter(char ch) {
    if (g_targets.empty()) return;
    if (g_prefix.empty()) { g_prefix = ch; Redraw(); return; }
    std::string want = g_prefix + ch;
    g_prefix.clear();
    int pick = -1;
    for (int i = 0; i < (int)g_targets.size() && pick < 0; i++)
        if (g_targets[i].label == want) pick = i;
    if (pick < 0) {
        POINT cc = HintCellCenter(g_area, want[0] - 'A', want[1] - 'A');
        long long best = -1;
        for (int i = 0; i < (int)g_targets.size(); i++) {
            long long d = Dist2(Center(g_targets[i].rc), cc);
            if (best < 0 || d < best) { best = d; pick = i; }
        }
    }
    g_moveTrail.clear();
    SelectTarget(pick);
}

void DoClick(Button b) {
    ExitClickMode();
    Click(b);
    TriggerClickFlash(true);
    if (b == Button::Left) AddMousePositionToStack();
}

int PaletteIndex(const Target& t) {
    return (t.label[0] - 'A') % (int)_countof(kPalette);
}

RECT DrawPill(HDC dc, const RECT& client, const std::wstring& text) {
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
    return r;
}

// 未选中的框和标签的不透明度（0-255）：半透，底下的内容还看得清；选中的那个和提示条是 255
constexpr BYTE kDimAlpha = 140;

// 画到 dc（客户区 rc = 整个屏幕区域）；opaque 收集要保持不透明的区域（选中的框 + 标签、提示条）
void DrawScene(HDC dc, const RECT& rc, std::vector<RECT>& opaque) {
    struct { HDC mem; RECT rc; } db = { dc, rc };
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
        if (i == g_sel) { InflateRect(&r, 3, 3); opaque.push_back(r); }
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
        if (i == g_sel) opaque.push_back(box);
        // 反色：未选中 = 深底 + 彩色字（首字母用本组颜色、第二个字母白色）；选中 = 亮绿底 + 黑字
        const bool sel = (i == g_sel);
        FillRect(db.mem, &box, sel ? selBg : border);
        FrameRect(db.mem, &box, sel ? border : brushes[PaletteIndex(t)]);
        int tx = x + 4;
        for (size_t k = 0; k < t.label.size(); k++) {
            // 已输入的字母变灰；两个字母颜色不同，两字母标签的边界一眼看清
            bool typed = k < g_prefix.size();
            COLORREF fg = sel ? (k == 0 ? RGB(0, 0, 0) : RGB(170, 0, 0))
                              : (k == 0 ? kPalette[PaletteIndex(t)] : RGB(255, 255, 255));
            SetTextColor(db.mem, typed ? RGB(120, 120, 120) : fg);
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

    if (!g_msg.empty()) opaque.push_back(DrawPill(db.mem, db.rc, g_msg));
}

// 逐像素 alpha 的分层窗口（UpdateLayeredWindow）：透明色 → 全透；opaque 区域 → 不透明；其余 → kDimAlpha
void Render() {
    if (!g_wnd || !g_clickMode) return;
    const int w = RectW(g_area), h = RectH(g_area);
    if (w <= 0 || h <= 0) return;
    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // 自上而下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(NULL, screen);
        return;
    }
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    SetBkMode(mem, TRANSPARENT);
    RECT rc = { 0, 0, w, h };
    std::vector<RECT> opaque;
    DrawScene(mem, rc, opaque);
    GdiFlush();

    const DWORD keyPx = ((DWORD)GetRValue(kKey) << 16) | ((DWORD)GetGValue(kKey) << 8) | GetBValue(kKey);
    std::vector<BYTE> rowA(w);
    DWORD* px = (DWORD*)bits;
    for (int y = 0; y < h; y++) {
        std::fill(rowA.begin(), rowA.end(), kDimAlpha);
        for (const RECT& o : opaque) {
            if (y < o.top || y >= o.bottom) continue;
            for (int x = max(0, (int)o.left); x < min(w, (int)o.right); x++) rowA[x] = 255;
        }
        DWORD* row = px + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            DWORD c = row[x] & 0xFFFFFF;
            if (c == keyPx) { row[x] = 0; continue; }
            DWORD a = rowA[x];
            DWORD r = ((c >> 16) & 0xFF) * a / 255, g = ((c >> 8) & 0xFF) * a / 255, b = (c & 0xFF) * a / 255;
            row[x] = (a << 24) | (r << 16) | (g << 8) | b;   // 预乘 alpha
        }
    }

    POINT dst = { g_area.left, g_area.top }, src = { 0, 0 };
    SIZE sz = { w, h };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(g_wnd, screen, &dst, &sz, mem, &src, 0, &bf, ULW_ALPHA);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

void OnScanDone(HWND hwnd, ScanResult* r) {
    if (r->gen != g_pend.gen) return;   // 过时的半份结果
    if (r->generic) { g_pend.haveGen = true; g_pend.gen_ = std::move(r->rects); }
    else            { g_pend.haveStd = true; g_pend.std_ = std::move(r->rects); }
    if (!g_pend.haveStd) return;        // 标准控件还没到：先不显示
    const bool complete = g_pend.haveGen;
    std::vector<RECT> rects = complete ? MergeGeneric(g_pend.std_, g_pend.gen_) : g_pend.std_;
    // 两半都到齐后写缓存；不管模式还在不在（用户可能已用缓存目标点完退出了）
    if (complete && (!g_cache.valid || (int)(r->gen - g_cache.gen) > 0)) {
        g_cache.valid = true;
        g_cache.gen = r->gen;
        g_cache.area = r->area;
        g_cache.sig = r->sig;
        g_cache.rects = rects;
    }
    if (r->gen != g_gen || !g_clickMode) return;
    if (rects.empty() && !complete) return;   // 标准控件没找到，等通用元素再说
    std::vector<Target> fresh = BuildTargets(std::move(rects));
    if (g_scanning) {
        g_scanning = false;
        g_targets = std::move(fresh);
    } else if (g_prefix.empty() && g_sel < 0 && !SameTargets(g_targets, fresh)) {
        g_targets = std::move(fresh);   // 屏上是缓存结果且用户还没开始选：换成最新结果
    } else {
        return;                         // 结果没变，或用户已经在选了：不打扰
    }
    if (g_targets.empty() && complete) {
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
        return 1;
    case WM_PAINT: {   // 内容全由 Render() 经 UpdateLayeredWindow 推上去，这里只确认失效区
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
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
    if (g_wnd) {
        // CreateOverlayWindow 用过 SetLayeredWindowAttributes，之后 UpdateLayeredWindow 会失败：
        // 摘掉再加回 WS_EX_LAYERED 重置成"逐像素 alpha"分层窗口
        LONG ex = GetWindowLongW(g_wnd, GWL_EXSTYLE);
        SetWindowLongW(g_wnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
        SetWindowLongW(g_wnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    }
    GetScanner(false).Post([] {});   // 提前起扫描线程、建好 UIA 对象，第一次进模式也省掉这段
    GetScanner(true).Post([] {});
}

void DestroyClickWindow() {
    if (g_wnd) { DestroyWindow(g_wnd); g_wnd = NULL; }
}

void EnterClickMode() {
    if (!g_wnd) return;
    if (g_clickMode) { ExitClickMode(); return; }   // 再按一次 = 关闭（开关）
    g_clickMode = true;
    BuildColumnBans();
    RefreshScreens();
    g_area = ScreenRectAt(GetCurrentScreenIndex());
    KillTimer(g_wnd, kExitTimer);
    g_sel = -1;
    g_moveTrail.clear();
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
    g_moveTrail.clear();
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
    // 已经输了坐标第一个字母：第二个字母 A-Z 全是坐标（包括 h/j/k/l/f/d），跟 Hint 一样
    if (!g_prefix.empty() && vk >= 'A' && vk <= 'Z' && !m.ctrl && !m.alt) {
        TypeLetter((char)vk);
        return true;
    }
    if (IsAction(Action::ClickMode, (WORD)vk, m)) { ExitClickMode(); return true; }   // d 是开关：再按一次关闭
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
        const unsigned opposite = bit == MV_LEFT ? MV_RIGHT : bit == MV_RIGHT ? MV_LEFT : bit == MV_UP ? MV_DOWN : MV_UP;
        if (!g_moveTrail.empty() && g_moveTrail.back().second == opposite) {   // 反方向：原路退回
            int from = g_moveTrail.back().first;
            g_moveTrail.pop_back();
            if (from >= 0 && from < (int)g_targets.size()) { SelectTarget(from); return true; }
            g_moveTrail.clear();
        }
        const int from = g_sel;
        int i = FindInDirection(bit);
        if (i >= 0) {
            if (from >= 0) g_moveTrail.push_back({ from, bit });
            SelectTarget(i);
        } else {
            Redraw();
        }
        return true;
    }
    // 坐标第一个字母：命令键（hjkl/f/g/d）以外的 A-Z
    if (vk >= 'A' && vk <= 'Z' && !m.ctrl && !m.alt && !IsCommandLetter((char)vk)) {
        TypeLetter((char)vk);
        return true;
    }
    return true;   // 其他键吞掉，避免误输入
}
