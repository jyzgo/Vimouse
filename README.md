# Vimouse 🖱️⌨️

*A keyboard-driven mouse controller for Vim lovers — Windows, tray-resident, zero dependencies.*

---

## 中文说明

**Vimouse** 用键盘完全接管鼠标：移动、点击、滚动、拖拽，外加 Hint / Grid 两种快速定位、屏幕标签、位置历史。运行后常驻托盘，无主窗口。

### 默认快捷键（全部可在「设置 → 快捷键」里改）

| 类别 | 按键 | 功能 |
|------|------|------|
| 开关 | `Ctrl+J` | 切换 Vimouse |
| | `Ctrl+Alt+K` | 切换并把光标放到屏幕中心 |
| | `Ctrl+Alt+J` | 切换远程模式（连接设置里的第一台主机） |
| | `Esc` | 退出当前模式 / 退出 Vimouse |
| | `Enter` | 左键点击 + 退出 |
| 移动 | `h/j/k/l` | 左/下/上/右，长按加速 |
| | `u/o/n/.` | 四个对角方向 |
| | `Shift+移动键` | 精确 1 像素 |
| 点击 | `f` | 左键（按住不松 = hold；`Shift+f` = Shift 点击） |
| | `g` / `b` | 右键 / 中键 |
| | `v` | 拖拽开关 |
| | `t` | 点击并在此处放标签 |
| 定位 | `m` | Hint：屏幕分 26×26 格，输两个字母跳到格子中心，然后自动进入一层微调 grid |
| | `i` | Grid：以光标为中心二分屏幕；grid 中再按 `i` 改为以屏幕中心二分 |
| | `c` | 跳到屏幕中心，连按切换显示器 |
| Grid 内 | `h/j/k/l` | 选左/下/上/右半区 |
| | `u/o/n/.` | 选四个象限 |
| | `r` | 返回上一级 |
| | `f/g` | 在当前位置点击并退出 |
| 标签 | `q` | 在光标处放置 / 移除标签 |
| | `w` | 标签跳转模式（按字母跳过去） |
| | `Backspace`（按住） | 临时放大显示所有标签 |
| 其他 | `y` | 滚轮模式：`j/k` 上下、`h/l` 左右 |
| | `r` / `e` | 回退 / 前进到历史点击位置 |

### 托盘菜单

- **启用键盘控制** — 开关
- **远程连接** — 有配置主机时出现
- **按键提示** — 在当前屏幕底部居中显示正在按的键，松开后约 0.25s 渐隐
- **悬浮帮助** — 可拖动的半透明速查窗（内容随你的键位配置变化，位置持久化）
- **操作指南** / **设置…** / **退出**

### 设置窗口

- **常规**：开机自启、按键提示、激活时是否使用十字准星光标
- **快捷键**：选一行 → 点「修改按键」→ 按下新键；有冲突会提示交换；可单项 / 全部恢复默认
- **远程**：SSH 主机列表（`host|远端 Vimouse.exe 路径`）

所有配置都在 `%USERPROFILE%\.vimouse\`：

| 文件 | 内容 |
|------|------|
| `settings.ini` | 常规开关 |
| `keymap.ini` | 快捷键，`action=Ctrl+Alt+J` 这种格式，可手改 |
| `tags.txt` | 屏幕标签 |
| `remote_hosts.txt` | 远程主机 |
| `help_pos.txt` | 悬浮帮助窗位置 |

### 视觉反馈

- **光标**：激活时变为十字准星（待机绿、移动中橙，中心是空心环不遮挡目标）
- **坐标标签**：光标右下角两个彩色字母 = 当前所在 Hint 格；点击时变大变黄
- **Grid**：十字 + 对角指引线，方向键标签按你的键位显示

### 管道 IPC / 命令行

其他程序可通过命名管道 `\\.\pipe\vimouse` 控制：

```
move x y            rmove dx dy          pos
click [x y]         rclick [x y]         dclick [x y]       mclick [x y]
mousedown [btn]     mouseup [btn]        drag x1 y1 x2 y2 [ms]
scroll up|down|left|right [n]
keypress ctrl+c     type 任意文本(UTF-8)
tags                tag <letter> [click]
status              screen               activate / deactivate
sleep ms            help
```

命令行：`Vimouse.exe -c "click 500 300"`、`Vimouse.exe -f script.txt`。
`mcp-vimouse/` 是配套的 MCP server，让 Claude 等 Agent 直接操作鼠标键盘。

### 远程模式

在 B 机上运行 Vimouse，A 机配置好 SSH 免密后，在 A 机按 `Ctrl+Alt+J`：A 机的按键会通过 `ssh B "Vimouse.exe --pipe-stdin"` 转成命令发到 B 机执行，指示器显示 `RMT`。

### 构建

Visual Studio 2022（v143，C++17，Windows SDK 10）：

```
msbuild Vimouse.sln -p:Configuration=Release -p:Platform=x64
```

产物在 `build/Release/Vimouse.exe`。

```
src/        C++ 源码，按模块拆分（Hook / Mover / Hint / Grid / Tags / KeyOsd / Settings …）
assets/     图标
tools/      gen_icon.py（生成图标）、remote-panel.py（多机面板）、wol.py（网络唤醒）
mcp-vimouse/ MCP server (Node)
```

---

## English

**Vimouse** is a lightweight keyboard-driven mouse controller for Vim users. Runs as a tray app; every key is rebindable from **Settings → Keys**.

| Key | Action |
|-----|--------|
| `Ctrl+J` | Toggle Vimouse |
| `h/j/k/l` | Move (hold to accelerate) · `Shift` = 1px |
| `u/o/n/.` | Diagonal move |
| `f` / `g` / `b` | Left (hold) / right / middle click |
| `v` | Drag toggle |
| `m` | Hint: type two letters to jump, then a one-level mini grid |
| `i` | Grid bisect (at cursor; press again for screen-center grid) |
| `y` | Scroll mode |
| `q` / `w` | Place tag / tag-jump mode |
| `c` | Screen center (repeat to cycle monitors) |
| `r/e` | Prev / next position history |
| `Enter` | Click + exit · `Esc` exit |

Tray menu: **Key OSD** (shows pressed keys at the bottom of the screen, fades on release), help overlay, settings (startup, cursor, keymap, SSH remote hosts).

Config lives in `%USERPROFILE%\.vimouse\` (`settings.ini`, `keymap.ini`, `tags.txt`, `remote_hosts.txt`).

Pipe IPC on `\\.\pipe\vimouse` — `Vimouse.exe -c help` lists commands. `mcp-vimouse/` wraps it as an MCP server.

Build: VS 2022, `msbuild Vimouse.sln -p:Configuration=Release -p:Platform=x64` → `build/Release/Vimouse.exe`.

---

## Inspiration

Inspired by [**warpd**](https://github.com/rvaiya/warpd), reimagined with Vim-native keybindings and system tray integration.

## Contact

Bug reports & suggestions: **jyzgo0125@gmail.com**

## License

MIT License — see [LICENSE](LICENSE).
