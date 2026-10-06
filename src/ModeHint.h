// ModeHint.h - 屏幕右侧的半透明、不可点击的「当前模式按键提示框」
// 按当前模式（鼠标 / Grid / Hint / 滚轮 / 标签 / 方向键）显示可用按键；未激活时隐藏。
// 开关：settings.ini 的 mode_hint（默认开启），设置窗口和托盘菜单都可切换。
#pragma once

void ModeHint_Create();    // 创建窗口并启动 100ms 状态轮询
void ModeHint_Destroy();
