// Cursor.h - 激活状态下的自定义系统光标（十字准星）
#pragma once

void SetVimouseCursor();     // 待机：绿色准星
void SetMovingCursor();      // 移动中：橙色准星
void RestoreSystemCursor();  // 恢复系统默认
void DestroyCustomCursors();
