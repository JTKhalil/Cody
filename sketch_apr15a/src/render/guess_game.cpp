#include "include/render/guess_game.h"

#include "include/globals.h"
#include "include/render/handdraw.h"

#include <cstdio>
#include <string.h>

static bool s_playing = false;
static bool s_show_answer = false;
static uint32_t s_end_ms = 0;
static uint32_t s_last_shown_sec = 0xFFFFFFFFu;
static char s_word[96] = {0};
/// 游戏结束（手动或到时）后右上角保留的剩余秒数；-1 表示不显示暂停态
static int s_timer_frozen_sec = -1;

// 右上角倒计时：背景条宽度随文字变化，减少遮挡画区；擦除区与上一帧取并集避免变窄留残影
static int s_timer_bg_x = 0;
static int s_timer_bg_y = 0;
static int s_timer_bg_w = 0;
static int s_timer_bg_h = 0;
static bool s_timer_bg_valid = false;

static inline int i_min(int a, int b) {
  return a < b ? a : b;
}
static inline int i_max(int a, int b) {
  return a > b ? a : b;
}

static void guess_invalidate_timer_bg() {
  s_timer_bg_valid = false;
}

// 底部揭晓：背景高度随文字行数收缩，文字基线贴屏幕下沿（y=239 为末行像素行）

/// 与揭晓条同一字体的 UTF-8 换行（wqy16 gb2312，行高与 draw_answer_overlay 一致）
static void print_wrapped_answer_gb2312(const String& text, int x, int yBaseline, int maxWidth, int maxBaselineY) {
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  const int lineHeight = u8g2.getFontAscent() - u8g2.getFontDescent() + 2;
  int currentX = x;
  int currentY = yBaseline;
  const int len = text.length();
  int i = 0;
  while (i < len) {
    const char c = text[i];
    int charBytes = 1;
    if ((c & 0x80) == 0)
      charBytes = 1;
    else if ((c & 0xE0) == 0xC0)
      charBytes = 2;
    else if ((c & 0xF0) == 0xE0)
      charBytes = 3;
    else if ((c & 0xF8) == 0xF0)
      charBytes = 4;
    if (i + charBytes > len) break;
    const String singleChar = text.substring(i, i + charBytes);
    if (c == '\n') {
      currentX = x;
      currentY += lineHeight;
      i += 1;
      continue;
    }
    const int w = u8g2.getUTF8Width(singleChar.c_str());
    if (currentX + w > x + maxWidth) {
      currentX = x;
      currentY += lineHeight;
      if (currentY > maxBaselineY) break;
    }
    u8g2.setCursor(currentX, currentY);
    u8g2.print(singleChar);
    currentX += w;
    i += charBytes;
    yield();
  }
}

/// 与 print_wrapped_answer_gb2312 相同换行规则，仅统计行数（须已 setFont wqy16 gb2312）
static int count_answer_wrap_lines(const String& text, int x, int maxWidth) {
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  int lines = 1;
  int currentX = x;
  const int len = text.length();
  int i = 0;
  while (i < len) {
    const char c = text[i];
    int charBytes = 1;
    if ((c & 0x80) == 0)
      charBytes = 1;
    else if ((c & 0xE0) == 0xC0)
      charBytes = 2;
    else if ((c & 0xF0) == 0xE0)
      charBytes = 3;
    else if ((c & 0xF8) == 0xF0)
      charBytes = 4;
    if (i + charBytes > len) break;
    const String singleChar = text.substring(i, i + charBytes);
    if (c == '\n') {
      lines++;
      currentX = x;
      i += 1;
      continue;
    }
    const int w = u8g2.getUTF8Width(singleChar.c_str());
    if (currentX + w > x + maxWidth) {
      currentX = x;
      lines++;
    }
    currentX += w;
    i += charBytes;
  }
  return lines;
}

static void format_mmss(char* out, size_t nout, int total_sec) {
  if (total_sec < 0) total_sec = 0;
  int m = total_sec / 60;
  int s = total_sec % 60;
  snprintf(out, nout, "%02d:%02d", m, s);
}

static void paint_timer_digits(int total_sec) {
  char buf[8];
  format_mmss(buf, sizeof(buf), total_sec);
  u8g2.setFont(u8g2_font_wqy12_t_chinese3);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(ST77XX_WHITE);
  const int tw = u8g2.getUTF8Width(buf);
  const int baselineY = 18;
  const int tx = 240 - tw - 4;
  const int ascent = u8g2.getFontAscent();
  const int descent = u8g2.getFontDescent();
  const int padX = 2;
  const int padY = 2;
  int yTop = baselineY - ascent - padY;
  if (yTop < 0) yTop = 0;
  const int boxH = (ascent - descent) + 2 * padY;
  const int rx = tx - padX;
  const int rw = tw + 2 * padX;
  const int ry = yTop;
  const int rh = boxH;

  uint16_t bg = handdraw_get_background_rgb565();
  int clearX = rx;
  int clearY = ry;
  int clearW = rw;
  int clearH = rh;
  if (s_timer_bg_valid) {
    clearX = i_min(rx, s_timer_bg_x);
    clearY = i_min(ry, s_timer_bg_y);
    const int clearR = i_max(rx + rw, s_timer_bg_x + s_timer_bg_w);
    const int clearB = i_max(ry + rh, s_timer_bg_y + s_timer_bg_h);
    clearW = clearR - clearX;
    clearH = clearB - clearY;
  }
  tft.fillRect(clearX, clearY, clearW, clearH, bg);

  s_timer_bg_x = rx;
  s_timer_bg_y = ry;
  s_timer_bg_w = rw;
  s_timer_bg_h = rh;
  s_timer_bg_valid = true;

  u8g2.setCursor(tx, baselineY);
  u8g2.print(buf);
}

static void draw_timer_live() {
  if (!s_playing) return;
  int32_t left = (int32_t)(s_end_ms - millis());
  if (left < 0) left = 0;
  int sec = (int)(left / 1000);
  paint_timer_digits(sec);
}

static void draw_timer_frozen() {
  if (s_timer_frozen_sec < 0) return;
  paint_timer_digits(s_timer_frozen_sec);
}

static void draw_answer_overlay() {
  if (!s_show_answer || s_word[0] == '\0') return;
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(ST77XX_WHITE);
  const String sw(s_word);
  const int maxSingle = 224;
  const int wrapX = 8;
  const int wrapMaxW = 220;
  const int tw = u8g2.getUTF8Width(sw.c_str());
  const int ascent = u8g2.getFontAscent();
  const int descent = u8g2.getFontDescent();  // 通常为负，表示基线以下像素
  const int lineH = ascent - descent + 2;
  const int bottomPad = 1;  // 与屏底：末行字重底边落在 y=239
  const int topPad = 2;     // 首行字顶上方少量衬底
  const int countedLines = (tw <= maxSingle) ? 1 : count_answer_wrap_lines(sw, wrapX, wrapMaxW);
  const int em = ascent - descent;
  int nCap = 1 + (240 - topPad - em - bottomPad) / lineH;
  if (nCap < 1) nCap = 1;
  const int useLines = (countedLines < nCap) ? countedLines : nCap;
  // 条带高度 ≈ 行数×行高 + 上下衬，整体上沿贴底（末行字重底约落在 y=239）
  const int stripH = topPad + (useLines - 1) * lineH + em + bottomPad;
  const int stripTop = 240 - stripH;
  const int firstBaseline = stripTop + topPad + ascent;
  const int lastBaseline = firstBaseline + (useLines - 1) * lineH;
  uint16_t bg = handdraw_get_background_rgb565();
  tft.fillRect(0, stripTop, 240, stripH, bg);
  if (tw <= maxSingle) {
    u8g2.setCursor((240 - tw) / 2, lastBaseline);
    u8g2.print(sw);
  } else {
    print_wrapped_answer_gb2312(sw, wrapX, firstBaseline, wrapMaxW, lastBaseline);
  }
}

void guess_game_start(const char* word_utf8, uint16_t seconds) {
  guess_invalidate_timer_bg();
  s_playing = false;
  s_show_answer = false;
  memset(s_word, 0, sizeof(s_word));
  if (!word_utf8 || word_utf8[0] == '\0') return;
  strncpy(s_word, word_utf8, sizeof(s_word) - 1);
  s_playing = true;
  s_timer_frozen_sec = -1;
  s_end_ms = millis() + (uint32_t)seconds * 1000u;
  s_last_shown_sec = 0xFFFFFFFFu;
  draw_timer_live();
}

void guess_game_end_round() {
  if (!s_playing && !s_show_answer) return;
  if (s_playing) {
    int32_t left = (int32_t)(s_end_ms - millis());
    if (left < 0) left = 0;
    s_timer_frozen_sec = (int)(left / 1000);
  }
  s_playing = false;
  s_show_answer = true;
  draw_timer_frozen();
  draw_answer_overlay();
}

void guess_game_tick() {
  if (!s_playing) return;
  uint32_t now = millis();
  if ((int32_t)(now - s_end_ms) >= 0) {
    guess_game_end_round();
    return;
  }
  uint32_t remain_sec = (s_end_ms - now) / 1000;
  if (remain_sec != s_last_shown_sec) {
    s_last_shown_sec = remain_sec;
    draw_timer_live();
  }
}

void guess_game_redraw_overlays() {
  if (s_playing) {
    draw_timer_live();
  } else if (s_show_answer && s_timer_frozen_sec >= 0) {
    draw_timer_frozen();
  }
  if (s_show_answer) draw_answer_overlay();
}

bool guess_game_skip_empty_hint() {
  return s_playing || s_show_answer;
}

void guess_game_reset() {
  guess_invalidate_timer_bg();
  s_playing = false;
  s_show_answer = false;
  s_timer_frozen_sec = -1;
  memset(s_word, 0, sizeof(s_word));
}

bool guess_game_is_playing() {
  return s_playing;
}

bool guess_game_is_showing_answer() {
  return s_show_answer;
}
