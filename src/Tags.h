// Tags.h - 屏幕位置标签（字母小窗），持久化到 tags.txt
#pragma once
#include <windows.h>

void LoadTags();                    // 启动时读取配置并创建标签窗口
void SaveTags();
void DestroyAllTags();

void PutTag(POINT p);               // 附近已有标签则移除，否则新建
bool JumpToTag(char letter);        // 成功返回 true 并退出标签模式

void EnterTagMode();                // 标签变大、不透明、可点击删除
void ExitTagMode();
void HideAllTagWindows();
void ShowTagWindowsNonInteractive();
