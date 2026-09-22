"""Vimouse 冒烟测试：启动 exe，走一遍管道命令 + 注入快捷键验证钩子/OSD，截图 OSD。
用法: python tools/smoke_test.py [build/Release/Vimouse.exe]
"""
import ctypes, os, subprocess, sys, time
from ctypes import wintypes

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "Release", "Vimouse.exe")
OUT = os.path.join(ROOT, "build", "smoke")
os.makedirs(OUT, exist_ok=True)

user32 = ctypes.windll.user32
KEYEVENTF_KEYUP = 0x0002
VK = {"ctrl": 0x11, "alt": 0x12, "shift": 0x10, "esc": 0x1B, "enter": 0x0D, ".": 0xBE}

def vk(k): return VK.get(k, ord(k.upper()) if len(k) == 1 else 0)
def key_down(k): user32.keybd_event(vk(k), 0, 0, 0)
def key_up(k):   user32.keybd_event(vk(k), 0, KEYEVENTF_KEYUP, 0)
def tap(*keys, hold=0.05):
    for k in keys: key_down(k)
    time.sleep(hold)
    for k in reversed(keys): key_up(k)
    time.sleep(0.08)

def cursor():
    p = wintypes.POINT(); user32.GetCursorPos(ctypes.byref(p)); return p.x, p.y

def pipe(cmd):
    r = subprocess.run([EXE, "-c", cmd], capture_output=True, text=True, timeout=5)
    return (r.stdout or r.stderr).strip()

def screenshot(path, bbox=None):
    """GDI 抓屏（含分层窗口），bbox=(l,t,r,b) 可选。"""
    from PIL import ImageGrab
    ImageGrab.grab(bbox=bbox, all_screens=False).save(path)

results = []
def check(name, ok, detail=""):
    results.append((name, ok, detail))
    print(f"  [{'OK ' if ok else 'FAIL'}] {name} {detail}")

# ---- 启动 ----
subprocess.run(["taskkill", "/IM", "Vimouse.exe", "/F"], capture_output=True)
time.sleep(0.5)
proc = subprocess.Popen([EXE], cwd=os.path.dirname(EXE))
time.sleep(2.0)
check("process alive", proc.poll() is None)

# ---- 管道 ----
print("--- pipe ---")
h = pipe("help");        check("help", h.startswith("OK commands"), h[:60])
s = pipe("status");      check("status", s.startswith("OK {"), s)
sc = pipe("screen");     check("screen", '"width"' in sc, sc)
pipe("move 600 400");    check("move", cursor() == (600, 400), str(cursor()))
p = pipe("pos");         check("pos", p == "OK 600 400", p)
t = pipe("tags");        check("tags", t.startswith("OK ["), t)
e = pipe("bogus");       check("unknown -> ERR", e.startswith("ERR"), e)
pipe("deactivate"); time.sleep(0.3)
check("deactivate", '"active":false' in pipe("status"))
pipe("activate"); time.sleep(0.3)
check("activate", '"active":true' in pipe("status"))

# ---- 钩子：激活状态下 h/j/k/l 移动光标 ----
print("--- hook ---")
pipe("move 800 500"); time.sleep(0.2)
key_down("l"); time.sleep(0.35); key_up("l"); time.sleep(0.2)
x, y = cursor(); check("hold L moves right", x > 800 and y == 500, f"{(x, y)}")
key_down("k"); time.sleep(0.35); key_up("k"); time.sleep(0.2)
x2, y2 = cursor(); check("hold K moves up", y2 < 500 and x2 == x, f"{(x2, y2)}")
key_down("shift"); tap("h", hold=0.03); key_up("shift"); time.sleep(0.15)
x3, _ = cursor(); check("Shift+H precise (few px)", 0 < x2 - x3 <= 6, f"dx={x2 - x3}")

# c → 屏幕中心
tap("c"); time.sleep(0.2)
sw, sh = user32.GetSystemMetrics(0), user32.GetSystemMetrics(1)
cx, cy = cursor(); check("C -> screen center", abs(cx - sw // 2) <= 2 and abs(cy - sh // 2) <= 2, f"{(cx, cy)} of {(sw, sh)}")

# 标签 q 放/移
pipe("move 700 300"); time.sleep(0.15)
tap("q"); time.sleep(0.3)
t1 = pipe("tags"); check("Q places tag", '"x":700' in t1, t1)
tap("q"); time.sleep(0.3)
t2 = pipe("tags"); check("Q again removes tag", '"x":700' not in t2, t2)

# Hint: m, 然后两个字母 → 落到格子中心，再 Esc 退出 mini grid
tap("m"); time.sleep(0.3)
tap("m"); tap("m"); time.sleep(0.4)
hx, hy = cursor()
exp_x = (sw * 12 // 26 + sw * 13 // 26) // 2; exp_y = (sh * 12 // 26 + sh * 13 // 26) // 2
check("Hint MM -> cell center", abs(hx - exp_x) <= 2 and abs(hy - exp_y) <= 2, f"{(hx, hy)} expect {(exp_x, exp_y)}")
tap("esc"); time.sleep(0.2)

# OSD：按住 Shift+J 时屏幕底部应出现 "Shift + J"，松开后渐隐
print("--- OSD ---")
pipe("move 1000 300"); time.sleep(0.15)
bottom = (sw // 2 - 320, sh - 260, sw // 2 + 320, sh - 20)
key_down("shift"); key_down("j"); time.sleep(0.2)
shot = os.path.join(OUT, "osd_hold.png"); screenshot(shot, bottom)
key_up("j"); key_up("shift"); time.sleep(0.6)
shot2 = os.path.join(OUT, "osd_released.png"); screenshot(shot2, bottom)
from PIL import Image, ImageChops
diff = ImageChops.difference(Image.open(shot).convert("RGB"), Image.open(shot2).convert("RGB")).getbbox()
check("OSD visible while held, gone after release", diff is not None, f"diff bbox={diff}")

# Ctrl+J 关，Ctrl+J 开
tap("ctrl", "j"); time.sleep(0.3)
check("Ctrl+J deactivates", '"active":false' in pipe("status"))
tap("ctrl", "j"); time.sleep(0.3)
check("Ctrl+J re-activates", '"active":true' in pipe("status"))

# 配置文件
cfg = os.path.join(os.environ["USERPROFILE"], ".vimouse")
check("tags.txt written", os.path.exists(os.path.join(cfg, "tags.txt")))

# ---- 收尾 ----
pipe("deactivate")
failed = [r for r in results if not r[1]]
print(f"\n{len(results) - len(failed)}/{len(results)} passed" + (f"  FAILED: {[r[0] for r in failed]}" if failed else ""))
print(f"process still alive: {proc.poll() is None}")
sys.exit(1 if failed else 0)
