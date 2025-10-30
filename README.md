# Vimouse 🖱️⌨️  
*A keyboard-driven mouse controller for Vim lovers*

---

## 中文说明

**Vimouse** 是一个专为 Vim 用户设计的小工具，让你完全通过键盘控制鼠标移动、点击和滚动，无需离开键盘。

### 快捷键说明

- **启动 Normal 模式**：`Ctrl + J` or `Crtl + Alt + K` 
  - 使用`Ctrl + Alt + K` 时,光标会自动居中
- **退出模式**：`Esc` 或 `Enter`  
  - 使用 `Enter` 退出时，会自动点击鼠标左键，用于聚焦当前窗口

#### Normal 模式
- `h` / `j` / `k` / `l`：鼠标向左 / 下 / 上 / 右移动
-  `u`/`o`/`n`/`.`: 分别代表 ↖️↗️ ↙️ ↘️的移动方向
- `f`：鼠标左键点击
- `g`：鼠标右键点击
  `r`: 返回上一个鼠标点击位置 (f)
  `e`: 返回下一个鼠标点击位置
- `y`：进入滚轮模式（Wheel Mode）  
  - 进入后，使用 `h`/`j`/`k`/`l` 控制页面滚动（模拟滚轮）
- `i`：进入网格模式（Grid Mode）
- `m`：进入地图模式（Hint Mode）
- `q`: 添加/移除 tag
  -在鼠标当前位置生成一个字母tag,可以进入tag模式导航
  -如果当前位置已经有tag,则会移除此tag
- `w`: 进入tag模式
#### Tag 模式
- 进入后屏幕上的tag会高亮,此时按对应的字母按键 光标会移动过去,之后自动退出Tag模式

#### Grid 模式
- 使用 `Q` / `W` / `A` / `S` 将鼠标移动到屏幕对应的四分之一区域
- 在此模式下按 `h`/`j`/`k`/`l` 会自动退出 Grid 模式

#### Hint 模式
- 按 `m` 后，屏幕会被覆盖上由两个字母组成的“坐标对”（如 AA, AB, AC...）
- 输入任意两个字母（如 `AB`），鼠标将立即移动到对应位置
- 支持 `Esc` 退出，或 `Enter` 退出并自动左键点击（用于聚焦）
- `r`：返回上一个区块（网格模式）
### 其他特性
- 兼容常用系统快捷键（如 `Ctrl+A` 全选、`Ctrl+C` 复制等），不会干扰正常操作
- 轻量、无依赖、启动即用

### 灵感来源
本项目深受 [warpd](https://github.com/rvaiya/warpd) 启发，但重新设计了更符合个人习惯的快捷键布局。

### 联系与维护
如有问题或发现 Bug，欢迎邮件联系：**jyzgo0125@gmail.com**  
> ⚠️ 注意：此项目为个人兴趣开发，因工作繁忙，**不会频繁更新**，敬请谅解。

---

## English README
---

# Vimouse  
*A keyboard-driven mouse controller for Vim lovers*

## Overview

**Vimouse** is a lightweight utility designed for Vim enthusiasts who want to control the mouse entirely from the keyboard—no hand movement required. Navigate, click, scroll, and tag positions on screen using intuitive, Vim-inspired keybindings.

Stay in the flow. Keep your fingers on the home row.

---

## Keybindings

### Entering Normal Mode
- **Activate Normal Mode**: `Ctrl + J` or `Ctrl + Alt + K`  
  - Using `Ctrl + Alt + K` centers the cursor on screen automatically.

### Exiting Any Mode 
- **Exit mode**: `Esc` or `Enter`  
  - Pressing `Enter` exits and **automatically left-clicks**, useful for focusing the current window.

---

### Normal Mode
- `h` / `j` / `k` / `l` → Move mouse left / down / up / right  
- `u` / `o` / `n` / `.` → Move diagonally: ↖️ / ↗️ / ↙️ / ↘️  
- `f` → Left mouse click  
- `g` → Right mouse click  
- `r` → Return to the **previous** clicked position  
- `e` → Go to the **next** clicked position (in history)  
- `y` → Enter **Wheel Mode**  
  - In Wheel Mode, use `h`/`j`/`k`/`l` to scroll horizontally or vertically (simulates mouse wheel)  
- `i` → Enter **Grid Mode**  
- `m` → Enter **Hint Mode** (also called "Map Mode")  
- `q` → Toggle **Tag** at current mouse position  
  - Creates a single-letter tag (e.g., `A`, `B`) at the cursor location  
  - If a tag already exists there, it is removed  
- `w` → Enter **Tag Mode**

---

### Tag Mode
- All defined tags appear highlighted on screen  
- Press the corresponding letter (e.g., `A`) to **move the cursor to that tag**  
- Tag Mode exits automatically after navigation

---

### Grid Mode
- `Q` / `W` / `A` / `S` → Jump mouse to one of the four screen quadrants:  
  - `Q`: Top-left  `W`: Top-right  
  - `A`: Bottom-left `S`: Bottom-right  
- Pressing `h`/`j`/`k`/`l` in Grid Mode **exits back to Normal Mode**

---

### Hint Mode (Map Mode)
- After pressing `m`, the screen is overlaid with two-letter coordinate hints (e.g., `AA`, `AB`, `AC`, …)  
- Type any **two-letter combination** (e.g., `AB`) to instantly move the mouse to that region  
- `Esc` → Exit without clicking  
- `Enter` → Exit and **left-click** (useful for activating UI elements)  
- `r` → Return to the last grid/hint block used

---

## Additional Features
- ✅ **Non-intrusive**: Common system shortcuts (e.g., `Ctrl+C`, `Ctrl+V`, `Ctrl+A`) are **passed through unaffected**  
- ✅ **Zero dependencies**: Runs as a single executable  
- ✅ **Instant launch**: No setup or configuration needed  
- ✅ **Low resource usage**: Minimal CPU and memory footprint

---

## Inspiration
Vimouse is heavily inspired by [**warpd**](https://github.com/rvaiya/warpd), but reimagined with a keybinding layout optimized for personal workflow and Vim muscle memory.

---

## Contact & Maintenance
Found a bug or have a suggestion? Feel free to reach out via email: **jyzgo0125@gmail.com**

> ⚠️ **Note**: This is a personal side project. Due to limited availability, **updates will be infrequent**. Your understanding is appreciated!

---

Happy keyboard mousing! 🖱️⌨️

## License

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.

```text
MIT License

Copyright (c) 2025 jyzgo

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
