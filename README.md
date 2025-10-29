Vimouse 🖱️⌨️
A keyboard-driven mouse controller for Vim lovers

中文说明

Vimouse 是一个专为 Vim 用户设计的小工具，让你完全通过键盘控制鼠标移动、点击和滚动，无需离开键盘。
快捷键说明
启动 Normal 模式：Ctrl + \
退出模式：Esc 或 Enter
使用 Enter 退出时，会自动点击鼠标左键，用于聚焦当前窗口
Normal 模式
h / j / k / l：鼠标向左 / 下 / 上 / 右移动
f：鼠标左键点击
g：鼠标右键点击
u：进入滚轮模式（Wheel Mode）
进入后，使用 h/j/k/l 控制页面滚动（模拟滚轮）
再次点击u键退出滚轮模式
i：进入网格模式（Grid Mode）
r：返回上一个区块（区域）
m：进入地图模式（Map Mode）
c：将鼠标移动到当前屏幕中央
连续快速按两次 c：跳转到下一个显示器（多屏支持）
Grid 模式
使用 Q / W / A / S 将鼠标移动到屏幕对应的四分之一区域
在此模式下按 h/j/k/l 会自动退出 Grid 模式
Map 模式
按 m 后，屏幕会被覆盖上由两个字母组成的“坐标对”（如 AA, AB, AC...）
输入任意两个字母（如 AB），鼠标将立即移动到对应位置
支持 Esc 退出，或 Enter 退出并自动左键点击（用于聚焦）
其他特性
兼容常用系统快捷键（如 Ctrl+A 全选、Ctrl+C 复制等），不会干扰正常操作
轻量、无依赖、启动即用
🚀 最佳实践（推荐工作流）
快速精准定位目标位置的高效组合：
1. 按 c 将鼠标移至当前屏幕中央；若使用多显示器，连按两次 c 可切换到其他屏幕；
2. 按 m 进入 Map 模式，输入两个字母快速跳转到目标大致区域；
3. 退出 Map 模式后，使用 h/j/k/l 微调鼠标位置；
4. 按 f 点击确认。

灵感来源
本项目深受 [warpd](https://github.com/rvaiya/warpd) 启发，但重新设计了更符合个人习惯的快捷键布局。
联系与维护
如有问题或发现 Bug，欢迎邮件联系：jyzgo0125@gmail.com
⚠️ 注意：此项目为个人兴趣开发，因工作繁忙，不会频繁更新，敬请谅解。

English README

Vimouse is a lightweight utility designed for Vim users who want to control the mouse entirely from the keyboard—no hand movement required.
Key Bindings
Enter Normal Mode: Ctrl + \
Exit Mode: Esc or Enter
Pressing Enter will also trigger a left mouse click to focus the current window
Normal Mode
h / j / k / l: Move mouse left / down / up / right
f: Left mouse click
g: Right mouse click
u: Enter Wheel Mode
Use h/j/k/l to scroll the page (simulates mouse wheel)
Click u ag will quit wheel mode.
i: Enter Grid Mode
r: Return to the previous screen region
m: Enter Map Mode
c: Move mouse to the center of the current screen
Double-tap c quickly: Jump to the next monitor (multi-monitor support)
Grid Mode
Use Q / W / A / S to move the mouse to one of the four screen quadrants
Pressing h/j/k/l in this mode will automatically exit Grid Mode
Map Mode
Press m to overlay the screen with two-letter coordinate labels (e.g., AA, AB, AC…)
Type any two letters (e.g., AB) to instantly move the mouse to that location
Exit with Esc, or Enter (which also performs a left click to focus the window)
Additional Features
Fully compatible with common system shortcuts (e.g., Ctrl+A, Ctrl+C) — no interference with normal typing or editing
Lightweight, dependency-free, and ready to use
🚀 Best Practice (Recommended Workflow)
For fast and precise mouse positioning:
1. Press c to center the cursor on the current screen; on multi-monitor setups, double-tap c to switch to another display;
2. Press m to enter Map Mode, then type two letters to jump to the approximate target area;
3. After exiting Map Mode, fine-tune the position with h/j/k/l;
4. Press f to click and confirm.
This hybrid approach combines coarse navigation with pixel-level control—maximizing efficiency!
Inspiration
Heavily inspired by [warpd](https://github.com/rvaiya/warpd), but with a completely rethought keybinding scheme that better fits my personal workflow.
Contact & Maintenance
For questions or bug reports, feel free to email: jyzgo0125@gmail.com
⚠️ Note: This is a personal side project. Due to a busy work schedule, updates will not be frequent. Sorry for any inconvenience!

License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

text
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
