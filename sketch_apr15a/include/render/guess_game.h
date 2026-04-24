#pragma once

#include <stdint.h>

/// 开始一轮：清空画布应在调用方先执行；此处只负责状态与右上角倒计时
void guess_game_start(const char* word_utf8, uint16_t seconds);
/// 结束一轮：停表，底部显示词语（需手绘模式）
void guess_game_end_round();
/// 主循环调用：倒计时到 0 自动结束
void guess_game_tick();
/// 每笔手绘 blit 之后调用，避免遮挡右上角时间与底部答案
void guess_game_redraw_overlays();
/// 无笔迹时的「请在小程序绘画」是否应隐藏（游戏中或揭晓时）
bool guess_game_skip_empty_hint();
/// 离开手绘模式时清除你画我猜状态（不单独重绘整屏）
void guess_game_reset();
/// 倒计时进行中（未手动/自动结束），此时禁止切换显示模式
bool guess_game_is_playing();
/// 本局已结束且正在显示底部答案（应拒绝远程/小程序绘画）
bool guess_game_is_showing_answer();
