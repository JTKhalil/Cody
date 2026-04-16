#pragma once

void drawClockFace();
void refreshDisplayByMode();
void printWrappedUTF8(String text, int x, int y, int maxWidth);
void displayNoteOnScreen();

// 系统信息页 / 长按重启进度提示
void drawSystemInfoPage();
void drawHoldProgress(const char* title, const char* hint, int secondsHeld, int totalSeconds);

// 开机页（小机器人 + Hello 文案）
void drawBootSplash();

