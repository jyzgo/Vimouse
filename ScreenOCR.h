#pragma once
#include "Common.h"

// 扫描屏幕区域，OCR 识别文字，返回 JSON 数组
// 格式: [{"text":"xxx","rect":[x1,y1,x2,y2]}, ...]
std::string ScanRegion(int x1, int y1, int x2, int y2);

// 读取鼠标位置附近的文字
std::string ReadAtCursor(int width = 300, int height = 60);

// 读取指定位置附近的文字
std::string ReadAt(int x, int y, int width = 300, int height = 60);

// 截取屏幕区域保存为 JPEG，返回文件路径
std::string CaptureScreenJPEG(int x1, int y1, int x2, int y2, int quality = 70);

// 截取当前屏幕（鼠标所在的那个）保存为 JPEG，可叠加坐标网格
// grid: 是否叠加字母坐标网格
// maxWidth: 输出最大宽度（0=不限制，默认1920）
std::string CaptureCurrentScreen(int quality = 70, bool grid = false, int maxWidth = 1920, bool grayscale = false);
