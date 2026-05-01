#include "include/render/handdraw.h"

#include "include/globals.h"
#include "include/render/guess_game.h"
#include <cstdio>
#include <string.h>

static constexpr int kW = 240;
static constexpr int kH = 240;
static constexpr size_t kBytes = (size_t)kW * (size_t)kH * 2u;

static constexpr const char* kStatePath = "/handdraw_state.txt";

// 静态缓冲区（约 112KiB）
static uint16_t s_hd[kW * kH];
static bool s_buffer_inited = false;
static uint16_t s_bg565 = 0x0000;
static bool s_has_drawing = false;

// 笔触高频路径不写盘：防抖持久化，避免整幅 handdraw.bin 阻塞主循环
static constexpr uint32_t kPersistDeferMs = 180;
static bool s_persist_pending = false;
static uint32_t s_persist_deadline_ms = 0;

static uint32_t s_last_remote_stroke_ms = 0;
static bool s_ever_remote_stroke = false;

static void handdraw_ensure_alloc() {
}

static void handdraw_fill_solid_bg() {
  for (size_t i = 0; i < (size_t)kW * (size_t)kH; i++) {
    s_hd[i] = s_bg565;
  }
}

static void handdraw_load_state_from_file() {
  s_bg565 = 0x0000;
  s_has_drawing = false;
  bool stateFound = false;

  if (LittleFS.exists(kStatePath)) {
    File f = LittleFS.open(kStatePath, "r");
    if (f) {
      String line = f.readStringUntil('\n');
      f.close();
      line.trim();
      unsigned bg = 0;
      int ha = 0;
      const int n = sscanf(line.c_str(), "%u %d", &bg, &ha);
      if (n >= 1) {
        s_bg565 = (uint16_t)(bg & 0xFFFFu);
        stateFound = true;
      }
      if (n >= 2) {
        s_has_drawing = (ha != 0);
      }
    }
  }

  // 旧版仅有 handdraw.bin、无状态文件：视为已有内容，锁定背景直至清屏
  if (!stateFound && LittleFS.exists("/handdraw.bin")) {
    File f = LittleFS.open("/handdraw.bin", "r");
    if (f && f.size() == (long)kBytes) {
      s_has_drawing = true;
    }
    if (f) f.close();
  }
}

static bool handdraw_write_bitmap_file() {
  File f = LittleFS.open("/handdraw.bin", "w");
  if (!f) return false;
  const size_t wn = f.write((const uint8_t*)s_hd, kBytes);
  f.close();
  return wn == kBytes;
}

static bool handdraw_write_state_file() {
  File f = LittleFS.open(kStatePath, "w");
  if (!f) return false;
  f.print((unsigned)s_bg565);
  f.print(" ");
  f.println(s_has_drawing ? 1 : 0);
  f.close();
  return true;
}

static bool handdraw_persist_to_storage() {
  if (!handdraw_write_bitmap_file()) return false;
  if (!handdraw_write_state_file()) return false;
  return true;
}

void handdraw_idle_tick() {
  if (!s_persist_pending) return;
  const uint32_t now = millis();
  if ((int32_t)(now - s_persist_deadline_ms) < 0) return;
  s_persist_pending = false;
  (void)handdraw_persist_to_storage();
}

void handdraw_flush_persist_now() {
  if (!s_persist_pending) return;
  s_persist_pending = false;
  (void)handdraw_persist_to_storage();
}

void handdraw_notify_ble_stroke_received() {
  s_last_remote_stroke_ms = millis();
  s_ever_remote_stroke = true;
}

bool handdraw_ble_idle_for_ms(uint32_t quiet_ms) {
  if (!s_ever_remote_stroke) return true;
  return (millis() - s_last_remote_stroke_ms) >= quiet_ms;
}

static void handdraw_load_from_storage() {
  handdraw_load_state_from_file();
  if (LittleFS.exists("/handdraw.bin")) {
    File f = LittleFS.open("/handdraw.bin", "r");
    if (f && f.size() == (long)kBytes) {
      f.read((uint8_t*)s_hd, kBytes);
      f.close();
      return;
    }
    if (f) f.close();
  }
  handdraw_fill_solid_bg();
}

/// 将 RAM 帧缓冲推到 TFT；无笔迹时叠提示（文字不在 s_hd 内，首笔前会先全屏重绘清字）
static void handdraw_present_framebuffer() {
  tft.drawRGBBitmap(0, 0, s_hd, kW, kH);
  if (!s_has_drawing && !guess_game_skip_empty_hint()) {
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setFontMode(1);
    u8g2.setForegroundColor(ST77XX_WHITE);
    // 提示语过长：拆为两行，并整体垂直居中显示
    const char* l1 = "请在\"Cody控制台\"";
    const char* l2 = "小程序端进行绘画";
    const int w1 = u8g2.getUTF8Width(l1);
    const int w2 = u8g2.getUTF8Width(l2);
    const int ascent = u8g2.getFontAscent();
    const int descent = u8g2.getFontDescent(); // 通常为负
    const int lineH = ascent - descent;
    const int gap = 4;
    const int totalH = lineH * 2 + gap;
    const int topY = (kH - totalH) / 2;
    const int y1 = topY + ascent;
    const int y2 = y1 + lineH + gap;

    u8g2.setCursor((kW - w1) / 2, y1);
    u8g2.print(l1);
    u8g2.setCursor((kW - w2) / 2, y2);
    u8g2.print(l2);
  }
  guess_game_redraw_overlays();
}

void handdraw_release_buffer() {
  handdraw_flush_persist_now();
  s_buffer_inited = false;
  s_bg565 = 0x0000;
  s_has_drawing = false;
  handdraw_fill_solid_bg();
}

void handdraw_on_mode_activate() {
  handdraw_ensure_alloc();
  if (!s_buffer_inited) {
    handdraw_load_from_storage();
    s_buffer_inited = true;
  }
  handdraw_present_framebuffer();
}

void handdraw_redraw_only() {
  if (!s_buffer_inited) {
    handdraw_on_mode_activate();
    return;
  }
  handdraw_present_framebuffer();
}

uint16_t handdraw_get_background_rgb565() {
  return s_bg565;
}

bool handdraw_has_locked_background() {
  return s_has_drawing;
}

bool handdraw_set_background_bw(bool black) {
  if (s_has_drawing) return false;
  s_persist_pending = false;
  s_bg565 = black ? (uint16_t)0x0000 : (uint16_t)0xFFFF;
  handdraw_fill_solid_bg();
  s_buffer_inited = true;
  handdraw_present_framebuffer();
  return handdraw_persist_to_storage();
}

static inline void plot_disk(int cx, int cy, int r, uint16_t c) {
  if (r < 1) r = 1;
  for (int y = cy - r; y <= cy + r; y++) {
    if (y < 0 || y >= kH) continue;
    for (int x = cx - r; x <= cx + r; x++) {
      if (x < 0 || x >= kW) continue;
      int dx = x - cx;
      int dy = y - cy;
      if (dx * dx + dy * dy <= r * r) {
        s_hd[(size_t)y * (size_t)kW + (size_t)x] = c;
      }
    }
  }
}

void handdraw_draw_segment(int x0, int y0, int x1, int y1, uint16_t rgb565, int widthPx) {
  if (guess_game_is_showing_answer()) return;
  handdraw_ensure_alloc();
  // 清掉空状态提示（提示仅画在屏上，不在缓冲里）
  if (!s_has_drawing) {
    tft.drawRGBBitmap(0, 0, s_hd, kW, kH);
  }
  widthPx = constrain(widthPx, 1, 24);
  int r = widthPx / 2;
  if (r < 1) r = 1;

  x0 = constrain(x0, 0, kW - 1);
  y0 = constrain(y0, 0, kH - 1);
  x1 = constrain(x1, 0, kW - 1);
  y1 = constrain(y1, 0, kH - 1);

  int dx = abs(x1 - x0);
  int dy = abs(y1 - y0);
  int sx = (x0 < x1) ? 1 : -1;
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx - dy;
  int x = x0;
  int y = y0;

  int minx = (x0 < x1 ? x0 : x1) - r - 2;
  int maxx = (x0 > x1 ? x0 : x1) + r + 2;
  int miny = (y0 < y1 ? y0 : y1) - r - 2;
  int maxy = (y0 > y1 ? y0 : y1) + r + 2;
  minx = constrain(minx, 0, kW - 1);
  maxx = constrain(maxx, 0, kW - 1);
  miny = constrain(miny, 0, kH - 1);
  maxy = constrain(maxy, 0, kH - 1);

  for (;;) {
    plot_disk(x, y, r, rgb565);
    if (x == x1 && y == y1) break;
    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x += sx;
    }
    if (e2 < dx) {
      err += dx;
      y += sy;
    }
  }

  s_has_drawing = true;

  const int blitW = maxx - minx + 1;
  const int blitH = maxy - miny + 1;
  if (blitW > 0 && blitH > 0) {
    // 减少 SPI 事务：整宽脏区一次推送；较小块先拷到补丁缓冲再一次推送
    if (blitW == kW) {
      tft.drawRGBBitmap(0, miny, s_hd + (size_t)miny * (size_t)kW, kW, blitH);
    } else {
      static constexpr int kPatchCap = 4096;
      static uint16_t s_patch[kPatchCap];
      const int pixels = blitW * blitH;
      if (pixels <= kPatchCap) {
        for (int row = 0; row < blitH; row++) {
          memcpy(s_patch + (size_t)row * (size_t)blitW,
                 s_hd + (size_t)(miny + row) * (size_t)kW + (size_t)minx,
                 (size_t)blitW * sizeof(uint16_t));
        }
        tft.drawRGBBitmap(minx, miny, s_patch, blitW, blitH);
      } else {
        for (int row = miny; row <= maxy; row++) {
          tft.drawRGBBitmap(minx, row, s_hd + (size_t)row * (size_t)kW + (size_t)minx, blitW, 1);
        }
      }
    }
  }

  s_persist_pending = true;
  s_persist_deadline_ms = millis() + kPersistDeferMs;
  guess_game_redraw_overlays();
}

void handdraw_clear_ram() {
  handdraw_ensure_alloc();
  s_persist_pending = false;
  s_has_drawing = false;
  // 清屏：仅清掉手绘缓冲。
  // - 游戏进行中：保留倒计时，不打断本局
  // - 已揭晓答案：清掉答案与冻结倒计时，允许继续绘画/开新局
  if (guess_game_is_showing_answer()) {
    guess_game_reset();
  }
  handdraw_fill_solid_bg();
  s_buffer_inited = true;
  handdraw_present_framebuffer();
  (void)handdraw_persist_to_storage();
}

bool handdraw_save_to_file() {
  s_persist_pending = false;
  return handdraw_persist_to_storage();
}

void handdraw_delete_saved() {
  s_persist_pending = false;
  LittleFS.remove("/handdraw.bin");
  LittleFS.remove(kStatePath);
  s_bg565 = 0x0000;
  s_has_drawing = false;
  handdraw_fill_solid_bg();
  s_buffer_inited = true;
  handdraw_present_framebuffer();
  (void)handdraw_persist_to_storage();
}

size_t handdraw_copy_pixels(uint8_t* dst, size_t offset, size_t max_len) {
  if (!dst || max_len == 0) return 0;
  if (offset >= kBytes) return 0;
  if (!s_buffer_inited) {
    handdraw_load_from_storage();
    s_buffer_inited = true;
  }
  size_t n = max_len;
  if (offset + n > kBytes) n = kBytes - offset;
  memcpy(dst, (const uint8_t*)s_hd + offset, n);
  return n;
}
