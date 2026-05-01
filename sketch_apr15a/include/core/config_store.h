#pragma once

void saveConfig();
void loadConfig();
void cleanupTempUploads();
void printFSSpace();

/// 格式化 LittleFS：清空图片、笔记等用户文件，并将轮播/笔记等内存状态恢复为默认（与开机默认值一致）。
void resetUserFilesystemToDefaults();

