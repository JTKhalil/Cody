#pragma once

#include <stdint.h>
#include <stddef.h>

/// 进入手绘模式时：确保缓冲区并从 LittleFS 载入（仅首次）后推到屏幕
void handdraw_on_mode_activate();
/// 同模式下调 refresh：仅把 RAM 缓冲区刷到 TFT
void handdraw_redraw_only();
void handdraw_release_buffer();

void handdraw_draw_segment(int x0, int y0, int x1, int y1, uint16_t rgb565, int widthPx);
void handdraw_clear_ram();
bool handdraw_save_to_file();
void handdraw_delete_saved();

uint16_t handdraw_get_background_rgb565();
bool handdraw_has_locked_background();
/// 仅当尚无笔迹时可切换；成功则整屏铺底并写入 LittleFS
bool handdraw_set_background_bw(bool black);

/// 拷贝手绘帧缓冲字节（与 /handdraw.bin 布局一致，小端 RGB565），用于 BLE 拉取同步
size_t handdraw_copy_pixels(uint8_t* dst, size_t offset, size_t max_len);

/// 主循环中调用：笔触停止一段时间后把 RAM 缓冲写入 LittleFS（降低跟笔延迟）
void handdraw_idle_tick();
/// 离开手绘模式 / 需要立即可读盘数据时调用
void handdraw_flush_persist_now();

/// BLE/JSON 收到远程笔迹时调用，用于离开手绘模式前的传输空窗判断
void handdraw_notify_ble_stroke_received();
/// 从未收到过远程笔迹视为空闲；否则需距最后一笔 >= quiet_ms（毫秒）
bool handdraw_ble_idle_for_ms(uint32_t quiet_ms);
