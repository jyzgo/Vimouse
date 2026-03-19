#pragma once
#include "Common.h"

// 扫描屏幕区域，OCR 识别文字，返回 JSON 数组
// 格式: [{"text":"xxx","rect":[x1,y1,x2,y2]}, ...]
std::string ScanRegion(int x1, int y1, int x2, int y2);

// 读取鼠标位置附近的文字
std::string ReadAtCursor(int width = 300, int height = 60);

// 读取指定位置附近的文字
std::string ReadAt(int x, int y, int width = 300, int height = 60);
