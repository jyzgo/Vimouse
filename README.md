# Vimouse 🖱️⌨️  
*A keyboard-driven mouse controller for Vim lovers*

---

## 中文说明

**Vimouse** 是一个专为 Vim 用户设计的键盘鼠标控制器，完全通过键盘控制鼠标移动、点击和滚动。运行后常驻系统托盘，无窗口。

### 快捷键

#### 激活/退出
| 按键 | 功能 |
|------|------|
| `Ctrl+J` | 切换 Vimouse 模式 |
| `Ctrl+Alt+K` | 切换并居中光标 |
| `Esc` | 退出当前模式/退出 Vimouse |
| `Enter` | 左键点击 + 退出 |

#### 移动
| 按键 | 功能 |
|------|------|
| `h/j/k/l` | 左/下/上/右（长按加速） |
| `u/o/n/.` | 左上/右上/左下/右下（对角） |
| `Shift+移动键` | 精确 1 像素移动 |

#### 点击
| 按键 | 功能 |
|------|------|
| `f` | 左键（按住不松=hold） |
| `g` | 右键 |
| `b` | 中键 |
| `Shift+f` | Shift+左键（范围选择） |
| `v` | 拖拽开关 |

#### 定位模式
| 按键 | 功能 |
|------|------|
| `m` | Hint 模式 — 输入两个字母跳转到屏幕坐标，之后自动进入 mini grid 精确微调 |
| `i` | Grid 二分模式 — 第一次在鼠标位置分屏，再按 `i` 切到屏幕中心分屏 |
| `c` | 跳到屏幕中心（连按切换多屏） |

#### Grid 模式操作
| 按键 | 功能 |
|------|------|
| `h/j/k/l` | 选择左/下/上/右半区 |
| `u/o/n/.` | 选择四个象限 |
| `r` | 返回上一级 |
| `i` | 退出 grid / 切换到屏幕中心 grid |
| `f/g` | 在当前位置左键/右键点击并退出 |

#### 标签系统
| 按键 | 功能 |
|------|------|
| `q` | 在当前位置放置/移除标签 |
| `w` | 进入标签跳转模式 |

#### 其他
| 按键 | 功能 |
|------|------|
| `y` | 滚轮模式（j/k 上下滚） |
| `r/e` | 回退/前进历史位置 |

### 系统托盘

右键托盘图标：
- **启用键盘控制** — 开关切换
- **悬浮帮助** — 可拖动的半透明快捷键参考窗口（位置持久化）
- **操作指南** — 弹出帮助对话框
- **退出**

### 管道 IPC

其他程序可通过命名管道 `\\.\pipe\vimouse` 发送命令控制鼠标键盘：

```
move x y          移动光标
click [x y]       左键点击
rclick [x y]      右键点击
dclick [x y]      双击
mclick [x y]      中键点击
drag x1 y1 x2 y2  拖拽
scroll up|down|left|right [n]  滚动
pos               获取光标位置
keypress combo    组合键（如 ctrl+c）
type text         输入文本（UTF-8）
tags / tag <letter> [click]  标签操作
status            查询状态
activate/deactivate  远程开关
screen            屏幕信息
help              命令列表
```

命令行模式：`Vimouse.exe -c "命令"` 或 `Vimouse.exe -f 脚本.txt`

### 视觉反馈

- **自定义光标**：激活时绿色十字准星，移动时橙色，退出恢复系统默认
- **坐标标签**：鼠标旁显示两个彩色字母（Hint 坐标），停止移动时更新
- **点击反馈**：点击时字母变大变黄
- **Hint 棋盘格**：交替背景色区分格子边界
- **Grid 指引线**：十字线 + 对角线 + 8 方向按键标签

---

## English

**Vimouse** is a lightweight keyboard-driven mouse controller for Vim users. Runs as a system tray application.

### Quick Reference

| Key | Action |
|-----|--------|
| `Ctrl+J` | Toggle Vimouse mode |
| `h/j/k/l` | Move left/down/up/right (hold to accelerate) |
| `Shift+move` | Precise 1-pixel movement |
| `u/o/n/.` | Diagonal movement |
| `f` | Left click (hold) |
| `g` | Right click |
| `b` | Middle click |
| `v` | Drag toggle |
| `m` | Hint mode (2-letter jump + mini grid) |
| `i` | Grid bisect (1st: at cursor, 2nd: screen center) |
| `y` | Scroll mode |
| `q` | Place/remove tag |
| `w` | Tag jump mode |
| `c` | Screen center (multi-press cycles monitors) |
| `r/e` | Prev/next position history |
| `Esc` | Exit mode |
| `Enter` | Click + exit |

### Pipe IPC

Control via named pipe `\\.\pipe\vimouse`:
```
move, click, rclick, dclick, mclick, drag, scroll, pos,
keypress, type, tags, tag, status, activate, deactivate, screen, help
```

CLI: `Vimouse.exe -c "command"` or `Vimouse.exe -f script.txt`

---

## Inspiration

Inspired by [**warpd**](https://github.com/rvaiya/warpd), reimagined with Vim-native keybindings and system tray integration.

## Contact

Bug reports & suggestions: **jyzgo0125@gmail.com**

## License

MIT License — see [LICENSE](LICENSE) for details.
