#pragma once

void drawClockFace();
/// 时钟模式：分钟变化时调用；尽量局部重绘时间，跨日则整屏重画，减少每分钟闪屏。
void drawClockFaceOnMinuteTick();
void refreshDisplayByMode();
void printWrappedUTF8(String text, int x, int y, int maxWidth);
void displayNoteOnScreen();

// 长按抹除进度提示
/** 长按抹除：elapsedMs/totalMs 平滑进度；静态说明与标题仅在 drawHoldProgressReset 后首帧绘制，避免闪烁 */
void drawSettingsEraseHoldProgress(uint32_t elapsedMs, uint32_t totalMs);
void drawHoldProgressReset();

// 开机页（小机器人 + Hello 文案）
/** @param showPcSerialConnected 为 true 时在 Hello Cody 下方显示「电脑已连接」（与 Logo 同屏、同生命周期） */
void drawBootSplash(bool showPcSerialConnected = false);

// 设置页（短按切换选项，长按执行）；离开子页/配网/抹除页后需整屏重绘，内部会局部刷新减闪烁
void invalidateSettingsMenuLayout();
void drawSettingsMenu(int selected);
/** 长按未松手时：选中行上浅绿条自左向右增长（0..1）；用于主菜单 */
void drawSettingsMenuLongPressProgress(int selected, float progress01);
/** 长按已显示进度但松手取消时：只重绘该行，避免整屏 invalidate 闪屏 */
void drawSettingsMenuClearLongPressProgress(int selected);
void drawSettingsSoftwareUpdateClearLongPressProgress(int subSelected);
/** 软件更新页双按钮时：同上 */
void drawSettingsSoftwareUpdateLongPressProgress(int subSelected, float progress01);
void drawSettingsNetStatus(int selected);
void drawSettingsSoftwareUpdate(int selected, const char* curVer, const char* latestVer, bool available, const char* hint,
                                const char* updateNotes);
void drawSettingsAbout(int selected);

// 电脑已连接：底部短暂提示条（叠画在现有画面上）
void drawPcSerialToastOverlay();

