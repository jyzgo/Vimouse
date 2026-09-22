// Indicator.h - 光标旁的两字母坐标标签 / 远程模式 RMT 标签 / 点击闪烁
#pragma once

void CreateIndicatorWindow();
void UpdateIndicatorPosition();        // 根据当前模式显示/隐藏并跟随光标
void TriggerClickFlash(bool autoEnd);  // autoEnd=true 300ms 后自动结束；false 需手动 EndClickFlash
void EndClickFlash();
void RefreshIndicator();               // 仅重绘
