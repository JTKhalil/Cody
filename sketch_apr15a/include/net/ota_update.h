#pragma once

void handleOtaInfo();
void handleDoUpdate();

// 供“设备端设置页”直接触发 OTA（与 handleDoUpdate 共用同一流程）
void startOtaUpdate();

// 拉取远端 version.txt，得到最新版本与更新内容（notes 为多行合并后的文本）
bool fetchRemoteVersionInfo(String& outLatest, String& outNotes);

// 版本按位比较：1.0.10 > 1.0.2；返回 1 / 0 / -1
int compareVersionParts(const char* a, const char* b);

