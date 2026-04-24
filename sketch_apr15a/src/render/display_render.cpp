#include "include/globals.h"
#include "include/render/display_render.h"
#include "include/render/expression_mode.h"
#include "include/render/handdraw.h"
#include "include/ble/cody_ble.h"

// forward decls from other modules
void loadSavedImage();

// 与 drawClockFace 成功绘制后同步，供 drawClockFaceOnMinuteTick 判断是否跨日需全屏重画
static int s_clockDom = -1;
static int s_clockMon = -1;
static int s_clockYear = -1;

// 大号「时:分」基线 Y（Adafruit 光标）；与 drawClockFaceOnMinuteTick 必须一致
static const int kClockTimeBaselineY = 68;
// 年月日、星期相对计算位置再整体上移（像素）
static const int kClockCalendarLiftPx = 25;

void drawTimeSyncingHint() {
  tft.fillScreen(ST77XX_BLACK);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(ST77XX_WHITE);
  const char* msg1 = "已连接小程序";
  const char* msg2 = "时间正在同步中...";
  int w1 = u8g2.getUTF8Width(msg1);
  int w2 = u8g2.getUTF8Width(msg2);
  u8g2.setCursor((240 - w1) / 2, 112);
  u8g2.print(msg1);
  u8g2.setForegroundColor(0xAD55);
  u8g2.setCursor((240 - w2) / 2, 140);
  u8g2.print(msg2);
  lastMinute = -1;
  s_clockDom = -1;
  s_clockMon = -1;
  s_clockYear = -1;
}

void drawClockFace() {
  // 重要：不要依赖 getLocalTime()（它与 SNTP 初始化强相关）。
  // BLE/串口 sync_time 使用 settimeofday() 后，time() 已可用；这里用 epoch 阈值判断是否已校时。
  time_t now = time(nullptr);
  const bool timeOk = (now >= (time_t)1700000000); // ~2023
  if (!timeOk) {
    tft.fillScreen(ST77XX_BLACK);
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setFontMode(1);
    u8g2.setForegroundColor(ST77XX_WHITE);
    const char* msg1 = "时间未校准";
    const char* msg2 = cody_ble_is_connected() ? "已连接小程序，正在同步时间" : "请连接 BLE 小程序校时";
    int w1 = u8g2.getUTF8Width(msg1);
    int w2 = u8g2.getUTF8Width(msg2);
    u8g2.setCursor((240 - w1) / 2, 112);
    u8g2.print(msg1);
    u8g2.setForegroundColor(0xAD55);
    u8g2.setCursor((240 - w2) / 2, 140);
    u8g2.print(msg2);
    // 与 loop 里「分钟变化才重画」对齐，避免刚切到时钟后立刻再全屏画一次导致闪屏
    lastMinute = -1;
    s_clockDom = -1;
    return;
  }
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  // Mario 风格 UI（简化像素元素）：天空/云/水管/地面
  const uint16_t SKY = tft.color565(92, 156, 255);
  const uint16_t CLOUD = tft.color565(252, 252, 252);
  const uint16_t CLOUD_S = tft.color565(210, 230, 255);
  const uint16_t GROUND = tft.color565(156, 92, 44);
  const uint16_t GROUND_D = tft.color565(120, 70, 30);
  const uint16_t GRASS = tft.color565(60, 200, 80);
  const uint16_t PIPE = tft.color565(20, 180, 70);
  const uint16_t PIPE_D = tft.color565(10, 120, 50);

  tft.fillScreen(SKY);

  auto drawCloud = [&](int x, int y, int s) {
    tft.fillCircle(x + 10 * s, y + 6 * s, 6 * s, CLOUD);
    tft.fillCircle(x + 18 * s, y + 4 * s, 7 * s, CLOUD);
    tft.fillCircle(x + 28 * s, y + 6 * s, 6 * s, CLOUD);
    tft.fillRoundRect(x + 8 * s, y + 6 * s, 26 * s, 10 * s, 6 * s, CLOUD);
    // 轻微阴影
    tft.drawRoundRect(x + 8 * s, y + 6 * s, 26 * s, 10 * s, 6 * s, CLOUD_S);
  };

  auto drawPipe = [&](int x, int y, int w, int h) {
    // 管口
    tft.fillRect(x - 6, y, w + 12, 10, PIPE);
    tft.drawRect(x - 6, y, w + 12, 10, PIPE_D);
    // 管身
    tft.fillRect(x, y + 10, w, h - 10, PIPE);
    tft.drawRect(x, y + 10, w, h - 10, PIPE_D);
    // 高光
    tft.fillRect(x + 3, y + 12, 4, h - 14, tft.color565(120, 255, 170));
  };

  // 云朵
  drawCloud(18, 18, 1);
  drawCloud(138, 34, 1);

  const int groundY = 188;
  // 水管先画，再铺地面与草坪，使管身被泥土/草盖住（仅在天空露出的管口仍可见）
  drawPipe(190, groundY - 36, 36, 74);

  // 地面（底部 52px）与草坪盖在水管下半段之上
  tft.fillRect(0, groundY, 240, 240 - groundY, GROUND);
  tft.fillRect(0, groundY, 240, 6, GRASS);
  for (int x = 0; x < 240; x += 16) {
    tft.drawFastVLine(x, groundY + 8, 240 - groundY - 8, GROUND_D);
  }
  for (int y = groundY + 10; y < 240; y += 14) {
    tft.drawFastHLine(0, y, 240, GROUND_D);
  }

  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

  // 时间：像素风边框字（白字+黑描边）
  tft.setTextSize(5);
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  int tx = (240 - w) / 2;
  const int ty = kClockTimeBaselineY;
  tft.setTextColor(ST77XX_BLACK);
  tft.setCursor(tx + 2, ty + 2);
  tft.print(timeStr);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(tx, ty);
  tft.print(timeStr);

  // 年月日、星期：在「时:分」像素下沿之下间隔 20px 起排字，水平居中（u8g2 的 y 为基线）
  int16_t tbX1, tbY1;
  uint16_t tw, th;
  tft.getTextBounds(timeStr, tx, ty, &tbX1, &tbY1, &tw, &th);
  // (tx,ty) 为基线：包围盒上沿为 ty+tbY1，下沿为 ty+tbY1+th-1；其下一行像素为 ty+tbY1+th
  const int belowTimeRow = ty + tbY1 + th;

  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  const int fa = u8g2.getFontAscent();
  const int fd = u8g2.getFontDescent(); // 通常为负
  // 日期首行像素上沿 = 时间下沿 + 20；基线 = 上沿 + fa
  int kDateBaselineY = belowTimeRow + 20 + fa;
  // 星期在日期下一行
  int kWeekBaselineY = kDateBaselineY + (fa - fd) + 6;
  // 保证整段在地面之上，避免画进土里（groundY 见上方）
  while (kWeekBaselineY - fd > groundY - 3) {
    kDateBaselineY -= 3;
    kWeekBaselineY -= 3;
  }
  kDateBaselineY -= kClockCalendarLiftPx;
  kWeekBaselineY -= kClockCalendarLiftPx;

  char dateStr[32];
  sprintf(dateStr, "%04d年%02d月%02d日", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
  // 避免其它页面把 fontMode(0)/背景色残留，导致中文文字出现黑底
  u8g2.setFontMode(1); // 透明背景
  u8g2.setForegroundColor(ST77XX_WHITE);
  int dw = u8g2.getUTF8Width(dateStr);
  u8g2.setCursor((240 - dw) / 2, kDateBaselineY);
  u8g2.print(dateStr);

  const char* weekdays[] = {"星期日","星期一","星期二","星期三","星期四","星期五","星期六"};
  u8g2.setForegroundColor(ST77XX_YELLOW);
  int ww = u8g2.getUTF8Width(weekdays[timeinfo.tm_wday]);
  u8g2.setCursor((240 - ww) / 2, kWeekBaselineY);
  u8g2.print(weekdays[timeinfo.tm_wday]);

  lastMinute = timeinfo.tm_min;
  s_clockDom = timeinfo.tm_mday;
  s_clockMon = timeinfo.tm_mon + 1;
  s_clockYear = timeinfo.tm_year + 1900;
}

void drawClockFaceOnMinuteTick() {
  time_t now = time(nullptr);
  if (now < (time_t)1700000000) return;
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);

  const int y = timeinfo.tm_year + 1900;
  const int mo = timeinfo.tm_mon + 1;
  const int d = timeinfo.tm_mday;

  // 未做过整屏时钟或日历未同步：走全屏
  if (s_clockDom < 0) {
    drawClockFace();
    return;
  }
  // 跨日/月/年：日期与星期需重画，直接全屏更简单且无接缝
  if (d != s_clockDom || mo != s_clockMon || y != s_clockYear) {
    drawClockFace();
    return;
  }

  const uint16_t SKY = tft.color565(92, 156, 255);
  // 仅清时间区域（与 kClockTimeBaselineY + 字号 5 匹配）
  tft.fillRect(12, kClockTimeBaselineY - 10, 216, 58, SKY);

  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  tft.setTextSize(5);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  const int tx = (240 - w) / 2;
  const int ty = kClockTimeBaselineY;
  tft.setTextColor(ST77XX_BLACK);
  tft.setCursor(tx + 2, ty + 2);
  tft.print(timeStr);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(tx, ty);
  tft.print(timeStr);

  lastMinute = timeinfo.tm_min;
}

void printWrappedUTF8(String text, int x, int y, int maxWidth) {
  int currentX = x;
  int currentY = y;
  int len = text.length();
  int i = 0;

  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  int lineHeight = u8g2.getFontAscent() - u8g2.getFontDescent() + 4;

  while (i < len) {
    char c = text[i];
    int charBytes = 1;
    if ((c & 0x80) == 0) charBytes = 1;
    else if ((c & 0xE0) == 0xC0) charBytes = 2;
    else if ((c & 0xF0) == 0xE0) charBytes = 3;
    else if ((c & 0xF8) == 0xF0) charBytes = 4;

    if (i + charBytes > len) break;
    String singleChar = text.substring(i, i + charBytes);

    if (c == '\n') {
      currentX = x;
      currentY += lineHeight;
      i += 1;
      continue;
    }

    int w = u8g2.getUTF8Width(singleChar.c_str());
    if (currentX + w > x + maxWidth) {
      currentX = x;
      currentY += lineHeight;
      if (currentY > 235) break;
    }

    u8g2.setCursor(currentX, currentY);
    u8g2.print(singleChar);
    currentX += w;
    i += charBytes;
    yield();
  }
}

void displayNoteOnScreen() {
  tft.fillScreen(ST77XX_BLACK);

  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setForegroundColor(ST77XX_YELLOW);
  String title = "我的笔记";
  int tw = u8g2.getUTF8Width(title.c_str());
  u8g2.setCursor((240 - tw) / 2, 25);
  u8g2.print(title);
  tft.drawFastHLine(20, 35, 200, 0x4208);

  if (!LittleFS.exists("/notes.json")) {
    u8g2.setForegroundColor(ST77XX_WHITE);
    u8g2.setCursor(10, 116);
    u8g2.print("暂无笔记");
    return;
  }

  StaticJsonDocument<4096> doc;
  File f = LittleFS.open("/notes.json", "r");
  deserializeJson(doc, f);
  f.close();

  int noteCount = doc.size();
  if (noteCount == 0) {
    u8g2.setForegroundColor(ST77XX_WHITE);
    u8g2.setCursor(10, 116);
    u8g2.print("笔记为空");
    return;
  }

  int targetIndex = noteCount - 1;
  if (pinnedNoteIndex >= 0 && pinnedNoteIndex < noteCount) {
    targetIndex = pinnedNoteIndex;
  } else if (noteSlideshowEnabled) {
    if (currentNoteDisplayIndex >= noteCount) currentNoteDisplayIndex = 0;
    targetIndex = currentNoteDisplayIndex;
  }

  JsonObject note = doc[targetIndex];
  String text = note["content"].as<String>();
  String timeStr = note["time"].as<String>();

  u8g2.setForegroundColor(ST77XX_GREEN);
  u8g2.setCursor(10, 71);
  if (targetIndex == pinnedNoteIndex) u8g2.print("置顶 · " + timeStr);
  else u8g2.print(timeStr);

  u8g2.setForegroundColor(ST77XX_WHITE);
  printWrappedUTF8(text, 10, 101, 220);
}

static void setBacklightMapped(int v) {
  v = constrain(v, 0, 255);
  int pwm = (v * v) / 255; // 与全局亮度设置一致的感知映射
  analogWrite(TFT_BLK, pwm);
}

static void fadeBacklightMapped(int fromV, int toV, int totalMs) {
  fromV = constrain(fromV, 0, 255);
  toV = constrain(toV, 0, 255);
  const int steps = 10;
  for (int i = 0; i <= steps; i++) {
    int v = fromV + (toV - fromV) * i / steps;
    setBacklightMapped(v);
    delay(totalMs / steps);
    yield();
  }
}

static void drawByModeOnce() {
  if (displayMode == 0) loadSavedImage();
  else if (displayMode == 1) drawClockFace();
  else if (displayMode == 2) displayNoteOnScreen();
  else if (displayMode == 3) expressionModeEnter();
  // displayMode == 4 手绘：由 handdraw_on_mode_activate / handdraw_redraw_only 单独处理
}

void refreshDisplayByMode() {
  // 仅在“模式变化”时做淡入淡出，避免时钟/笔记刷新时频繁闪烁
  static int lastMode = -1;
  const bool modeChanged = (displayMode != lastMode);

  if (!modeChanged) {
    if (displayMode == 4) {
      handdraw_redraw_only();
      return;
    }
    drawByModeOnce();
    return;
  }

  // 第一次进入（开机）不做过渡，避免启动变慢
  if (lastMode < 0) {
    lastMode = displayMode;
    if (displayMode == 4) {
      handdraw_on_mode_activate();
    } else {
      drawByModeOnce();
    }
    // 确保背光与逻辑亮度一致
    setBacklightMapped(backlightValue);
    return;
  }

  const int prevModeForFlush = lastMode;
  lastMode = displayMode;
  if (prevModeForFlush == 4 && displayMode != 4) {
    handdraw_flush_persist_now();
  }

  // 过渡：先几乎关背光，再画新模式，最后淡入——避免旧画面与新内容叠在一起造成“闪屏/鬼影”
  const int target = constrain(backlightValue, 0, 255);
  fadeBacklightMapped(target, 0, 100);
  // 时钟模式会整屏 fill 天空色或黑底占位，此处不再先铺纯黑，减少背光渐亮时一帧黑闪
  if (displayMode != 1) {
    tft.fillScreen(ST77XX_BLACK);
  }
  if (displayMode == 4) {
    handdraw_on_mode_activate();
  } else {
    drawByModeOnce();
  }
  fadeBacklightMapped(0, target, 170);
}

static void drawClaudeBotPixelArt(int centerX, int topY, int s, uint16_t eyeColor) {
  // 轻量像素画：用 fillRect 放大像素块绘制（避免塞位图占空间）
  // 机器人颜色：与表情模式背景同款橙色
  const uint16_t BOT = tft.color565(218, 17, 0);
  const uint16_t EYE = eyeColor;

  // 以 centerX 为中心绘制
  // 主体：宽 10s，高 6s（纵向拉伸，避免“扁”）；左右“耳朵”各 2s
  int bodyW = 10 * s;
  int bodyH = 6 * s;
  int x0 = centerX - bodyW / 2;
  int y0 = topY;

  // 主体
  tft.fillRect(x0, y0, bodyW, bodyH, BOT);
  // 左右耳朵（像素感）
  tft.fillRect(x0 - 2 * s, y0 + 2 * s, 2 * s, 2 * s, BOT);
  tft.fillRect(x0 + bodyW, y0 + 2 * s, 2 * s, 2 * s, BOT);

  // 眼睛：两个 1s 方块
  tft.fillRect(x0 + 2 * s, y0 + 2 * s, s, s, EYE);
  tft.fillRect(x0 + 7 * s, y0 + 2 * s, s, s, EYE);

  // 腿：4 条，每条宽 1s 高 2s（更短一些）
  int legY = y0 + bodyH;
  int legH = 2 * s;
  tft.fillRect(x0 + 1 * s, legY, s, legH, BOT);
  tft.fillRect(x0 + 3 * s, legY, s, legH, BOT);
  tft.fillRect(x0 + 6 * s, legY, s, legH, BOT);
  tft.fillRect(x0 + 8 * s, legY, s, legH, BOT);
}

void drawBootSplash(bool showPcSerialConnected) {
  // 暗色底 + 橙色像素机器人 + Hello Cody（可选：PC 串口已连时在其下显示「电脑已连接」）
  const uint16_t bgDark = ST77XX_BLACK;
  tft.fillScreen(bgDark);

  const int s = 12; // 像素块大小
  const int bodyH = 6 * s;
  const int legH = 2 * s;
  const int botH = bodyH + legH;

  const char* msg = "Hello Cody";
  tft.setTextColor(ST77XX_WHITE);
  const int textSize = 2;
  tft.setTextSize(textSize);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);

  const int gap = 6; // logo 与 Hello 行间距
  // 第二行中文与间距（与 Logo 一并参与垂直居中）
  const int pcExtra = showPcSerialConnected ? (10 + 18) : 0;
  const int groupH = botH + gap + (int)h + pcExtra;
  const int topY = (240 - groupH) / 2;

  drawClaudeBotPixelArt(120, topY, s, ST77XX_BLACK);

  int textX = (240 - (int)w) / 2;
  int textY = topY + botH + gap;
  tft.setCursor(textX, textY);
  tft.print(msg);
  tft.setCursor(textX + 1, textY);
  tft.print(msg);

  if (showPcSerialConnected) {
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setFontMode(1);
    u8g2.setForegroundColor(ST77XX_GREEN);
    const char* hint = "电脑已连接";
    int hw = u8g2.getUTF8Width(hint);
    const int hintBaseY = textY + (int)h + 10 + 16;
    u8g2.setCursor((240 - hw) / 2, hintBaseY);
    u8g2.print(hint);
  }
}

static void drawKeyValueLine(int y, const String& key, const String& val, uint16_t keyColor, uint16_t valColor) {
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setForegroundColor(keyColor);
  u8g2.setCursor(10, y);
  u8g2.print(key);
  u8g2.setForegroundColor(valColor);
  u8g2.setCursor(10, y + 20);
  u8g2.print(val);
}

/** 主设置列表是否已整屏绘制；子页/其它全屏界面会将其置为 false，便于返回时重画 */
static bool g_settingsMenuListValid = false;
static int g_settingsMenuLastSelected = -1;

/** 长按进度增量绘制状态（仅刷新新增浅绿条，减轻闪屏） */
static int16_t s_lpDeltaLastFw = -1;
static uint32_t s_lpDeltaCellKey = 0xFFFFFFFFu;
static unsigned long s_lpDeltaLastTextMs = 0;

static void resetLongPressProgressDeltaState() {
  s_lpDeltaLastFw = -1;
  s_lpDeltaCellKey = 0xFFFFFFFFu;
  s_lpDeltaLastTextMs = 0;
}

void invalidateSettingsMenuLayout() {
  g_settingsMenuListValid = false;
  g_settingsMenuLastSelected = -1;
  resetLongPressProgressDeltaState();
}

static void drawSettingsHeader(const char* title) {
  invalidateSettingsMenuLayout();
  tft.fillScreen(ST77XX_BLACK);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(ST77XX_CYAN);
  u8g2.setCursor(10, 24);
  u8g2.print(title);
  tft.drawFastHLine(10, 32, 220, 0x4208);
}

static void drawMenuItem(int y, const char* text, bool selected) {
  const uint16_t bg = selected ? tft.color565(60, 140, 255) : ST77XX_BLACK;
  const uint16_t fg = selected ? ST77XX_BLACK : ST77XX_WHITE;
  tft.fillRect(10, y - 18, 220, 26, bg);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(fg);
  u8g2.setCursor(18, y);
  u8g2.print(text);
}

/** 与 drawMenuItem 一致；长按进度时底色=选中蓝，左侧浅绿矩形为进度（直角） */
static void drawMenuItemWithLongPressProgress(int y, const char* text, bool selected, float progress01) {
  const int bx = 10;
  const int by = y - 18;
  const int bw = 220;
  const int bh = 26;
  const uint16_t selBlue = tft.color565(60, 140, 255);
  const uint16_t progLightGreen = tft.color565(130, 235, 170);

  if (progress01 < 0.f) progress01 = 0.f;
  if (progress01 > 1.f) progress01 = 1.f;

  if (!selected) {
    resetLongPressProgressDeltaState();
    tft.fillRect(bx, by, bw, bh, ST77XX_BLACK);
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setFontMode(1);
    u8g2.setForegroundColor(ST77XX_WHITE);
    u8g2.setCursor(18, y);
    u8g2.print(text);
    return;
  }

  if (progress01 <= 0.f) {
    resetLongPressProgressDeltaState();
    tft.fillRect(bx, by, bw, bh, selBlue);
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setFontMode(1);
    u8g2.setForegroundColor(ST77XX_BLACK);
    u8g2.setCursor(18, y);
    u8g2.print(text);
    return;
  }

  const int fw = (int)((float)bw * progress01 + 0.5f);
  const uint32_t cellKey = (uint32_t)(uint16_t)by << 16 | (uint16_t)bx;
  const unsigned long nowMs = millis();
  if (cellKey != s_lpDeltaCellKey) {
    s_lpDeltaCellKey = cellKey;
    s_lpDeltaLastFw = -1;
  }

  auto drawLabel = [&]() {
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setFontMode(1);
    u8g2.setForegroundColor(ST77XX_BLACK);
    u8g2.setCursor(18, y);
    u8g2.print(text);
  };

  if (s_lpDeltaLastFw < 0) {
    tft.fillRect(bx, by, bw, bh, selBlue);
    if (fw >= bw) {
      tft.fillRect(bx, by, bw, bh, progLightGreen);
    } else if (fw > 0) {
      tft.fillRect(bx, by, fw, bh, progLightGreen);
    }
    s_lpDeltaLastFw = (int16_t)fw;
    s_lpDeltaLastTextMs = nowMs;
    drawLabel();
    return;
  }

  if (fw == s_lpDeltaLastFw) {
    return;
  }

  if (fw < s_lpDeltaLastFw) {
    tft.fillRect(bx, by, bw, bh, selBlue);
    if (fw >= bw) {
      tft.fillRect(bx, by, bw, bh, progLightGreen);
    } else if (fw > 0) {
      tft.fillRect(bx, by, fw, bh, progLightGreen);
    }
    s_lpDeltaLastFw = (int16_t)fw;
    s_lpDeltaLastTextMs = nowMs;
    drawLabel();
    return;
  }

  const int prevFw = (int)s_lpDeltaLastFw;
  tft.fillRect(bx + prevFw, by, fw - prevFw, bh, progLightGreen);
  s_lpDeltaLastFw = (int16_t)fw;

  const int textStartX = 18;
  const bool overlapsText = (bx + fw) > textStartX;
  const bool crossedIntoText = (bx + prevFw) <= textStartX && (bx + fw) > textStartX;
  if (overlapsText && (crossedIntoText || (int)(nowMs - s_lpDeltaLastTextMs) >= 48 || fw >= bw)) {
    s_lpDeltaLastTextMs = nowMs;
    drawLabel();
  }
}

void drawSettingsMenuLongPressProgress(int selected, float progress01) {
  // wxcody-ble 固件：移除 WiFi/Web 相关设置入口
  static const char* items[] = {"退出", "连接说明", "关于本机"};
  constexpr int kN = 3;
  if (selected < 0) selected = 0;
  if (selected > kN - 1) selected = kN - 1;
  if (!g_settingsMenuListValid) return;
  const int firstY = 44;
  const int lineStep = 30;
  // 只重绘当前选中行，避免六行文字每帧重画导致闪屏
  const int y = firstY + selected * lineStep;
  drawMenuItemWithLongPressProgress(y, items[selected], true, progress01);
}

void drawSettingsMenuClearLongPressProgress(int selected) {
  // wxcody-ble 固件：移除 WiFi/Web 相关设置入口
  static const char* items[] = {"退出", "连接说明", "关于本机"};
  constexpr int kN = 3;
  if (selected < 0) selected = 0;
  if (selected > kN - 1) selected = kN - 1;
  if (!g_settingsMenuListValid) {
    drawSettingsMenu(selected);
    return;
  }
  resetLongPressProgressDeltaState();
  const int firstY = 44;
  const int lineStep = 30;
  const int y = firstY + selected * lineStep;
  drawMenuItem(y, items[selected], true);
}

void drawSettingsSoftwareUpdateClearLongPressProgress(int subSelected) {
  resetLongPressProgressDeltaState();
  if (subSelected < 0) subSelected = 0;
  if (subSelected > 1) subSelected = 1;
  drawMenuItem(168, "返回", subSelected == 0);
  drawMenuItem(198, "开始更新", subSelected == 1);
}

void drawSettingsSoftwareUpdateLongPressProgress(int subSelected, float progress01) {
  if (subSelected < 0) subSelected = 0;
  if (subSelected > 1) subSelected = 1;
  // 只重绘当前选中的按钮，另一枚保持 drawSettingsSoftwareUpdate 已绘制内容
  if (subSelected == 0) {
    drawMenuItemWithLongPressProgress(168, "返回", true, progress01);
  } else {
    drawMenuItemWithLongPressProgress(198, "开始更新", true, progress01);
  }
}

void drawSettingsBleInfoLongPressProgress(int subSelected, float progress01) {
  if (subSelected < 0) subSelected = 0;
  if (subSelected > 1) subSelected = 1;
  // 只重绘当前选中的按钮，避免整屏闪烁
  if (subSelected == 0) {
    drawMenuItemWithLongPressProgress(168, "返回", true, progress01);
  } else {
    drawMenuItemWithLongPressProgress(198, "删除信任设备", true, progress01);
  }
}

static void drawBottomHint(const char* leftHint, const char* rightHint) {
  tft.fillRect(0, 214, 240, 26, ST77XX_BLACK);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(0xAD55);
  u8g2.setCursor(10, 236);
  u8g2.print(leftHint ? leftHint : "短按切换");
  int w = u8g2.getUTF8Width(rightHint ? rightHint : "长按执行");
  u8g2.setCursor(240 - 10 - w, 236);
  u8g2.print(rightHint ? rightHint : "长按执行");
}

void drawPcSerialToastOverlay() {
  tft.fillRect(0, 208, 240, 32, ST77XX_BLACK);
  tft.drawFastHLine(0, 208, 240, 0x4208);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(ST77XX_GREEN);
  const char* msg = "电脑已连接";
  int w = u8g2.getUTF8Width(msg);
  u8g2.setCursor((240 - w) / 2, 230);
  u8g2.print(msg);
}

void drawSettingsMenu(int selected) {
  // wxcody-ble 固件：移除 WiFi/Web 相关设置入口
  static const char* items[] = {"退出", "连接说明", "关于本机"};
  constexpr int kN = 3;
  if (selected < 0) selected = 0;
  if (selected > kN - 1) selected = kN - 1;
  const int firstY = 44;
  const int lineStep = 30;

  if (!g_settingsMenuListValid) {
    tft.fillScreen(ST77XX_BLACK);
    tft.drawFastHLine(10, 34, 220, 0x4208);
    for (int i = 0; i < kN; i++) {
      drawMenuItem(firstY + i * lineStep, items[i], selected == i);
    }
    drawBottomHint("短按切换", "长按执行");
    g_settingsMenuLastSelected = selected;
    g_settingsMenuListValid = true;
    return;
  }

  if (g_settingsMenuLastSelected != selected) {
    const int prev = g_settingsMenuLastSelected;
    if (prev >= 0 && prev < kN) {
      drawMenuItem(firstY + prev * lineStep, items[prev], false);
    }
    drawMenuItem(firstY + selected * lineStep, items[selected], true);
    g_settingsMenuLastSelected = selected;
  }
}

void drawSettingsBleInfo(int selected) {
  drawSettingsHeader("连接说明");

  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setFontMode(1);

  const String name = String(cody_ble_get_name());
  const bool hasTrusted = cody_ble_has_trusted();
  const String trustedName = String(cody_ble_get_trusted_name());

  u8g2.setForegroundColor(ST77XX_ORANGE);
  // 缩小行间距，给底部按钮留空间
  u8g2.setCursor(10, 62);
  u8g2.print("蓝牙名称");
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(10, 82);
  u8g2.print(name);

  if (!hasTrusted) {
    u8g2.setForegroundColor(ST77XX_ORANGE);
    u8g2.setCursor(10, 136);
    u8g2.print("连接方法");
    u8g2.setForegroundColor(ST77XX_WHITE);
    u8g2.setCursor(10, 162);
    u8g2.print("请在微信搜索“Cody控制台”");
    u8g2.setCursor(10, 186);
    u8g2.print("小程序进行连接和管理Cody");

    u8g2.setForegroundColor(0xAD55);
    u8g2.setCursor(10, 236);
    u8g2.print("短按返回");
    return;
  }

  // 已有信任设备：显示名称，隐藏连接方法；并提供“删除信任设备”按钮
  u8g2.setForegroundColor(ST77XX_ORANGE);
  u8g2.setCursor(10, 112);
  u8g2.print("信任设备名称");
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(10, 132);
  u8g2.print(trustedName.length() ? trustedName.c_str() : "-");

  // bottom actions (short press to switch, long press to execute)
  if (selected < 0) selected = 0;
  if (selected > 1) selected = 1;
  drawMenuItem(168, "返回", selected == 0);
  drawMenuItem(198, "删除信任设备", selected == 1);
  drawBottomHint("短按切换", "长按执行");
}

void drawBlePairPrompt(const char* peer) {
  invalidateSettingsMenuLayout();
  tft.fillScreen(ST77XX_BLACK);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setFontMode(1);

  u8g2.setForegroundColor(ST77XX_CYAN);
  u8g2.setCursor(10, 24);
  u8g2.print("蓝牙连接请求");
  tft.drawFastHLine(10, 32, 220, 0x4208);

  u8g2.setForegroundColor(0xAD55);
  u8g2.setCursor(10, 66);
  u8g2.print("请求连接的设备名称");
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(10, 90);
  u8g2.print(peer ? peer : "-");

  // 中部进度条（长按过程中更新）
  tft.drawRect(30, 138, 180, 14, 0x4208);
  tft.fillRect(31, 139, 178, 12, ST77XX_BLACK);
  // 初始 0%
  u8g2.setForegroundColor(0xAD55);
  u8g2.setCursor(108, 174);
  u8g2.print("0%");

  // bottom hint
  tft.fillRect(0, 214, 240, 26, ST77XX_BLACK);
  u8g2.setForegroundColor(0xAD55);
  u8g2.setCursor(10, 236);
  u8g2.print("短按拒绝 / 长按允许");
}

void drawBlePairProgress(uint8_t pct) {
  if (pct > 100) pct = 100;
  // 仅更新进度条区域，避免闪屏
  const int x = 31, y = 139, w = 178, h = 12;
  const int fillW = (int)((w * (int)pct) / 100);
  tft.fillRect(x, y, w, h, ST77XX_BLACK);
  if (fillW > 0) {
    tft.fillRect(x, y, fillW, h, tft.color565(130, 235, 170));
  }
  // 百分比文字：只擦一小条文本区
  tft.fillRect(92, 162, 60, 18, ST77XX_BLACK);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(0xAD55);
  char buf[8];
  snprintf(buf, sizeof(buf), "%u%%", (unsigned)pct);
  int tw = u8g2.getUTF8Width(buf);
  u8g2.setCursor((240 - tw) / 2, 176);
  u8g2.print(buf);
}

static int utf8FirstCharLen(uint8_t c) {
  if ((c & 0x80) == 0) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

/** 在软件更新页绘制远端 version.txt 正文（wqy12，较标题小一号；自动换行，y 为 u8g2 基线） */
static void drawSoftwareUpdateNotesWrapped(int x, int& y, int yMax, const char* utf8) {
  if (!utf8 || !utf8[0]) return;
  String rest(utf8);
  rest.trim();
  if (rest.length() == 0) return;

  // 较 wqy16 小一号；使用 chinese3 子集以控制 Flash（全 gb2312 会撑爆 OTA 分区）
  u8g2.setFont(u8g2_font_wqy12_t_chinese3);
  u8g2.setFontMode(1);
  const int maxW = 220;
  const int lineStep = 14;

  while (rest.length() > 0) {
    if (y > yMax) break;
    int nl = rest.indexOf('\n');
    String segment = (nl >= 0) ? rest.substring(0, nl) : rest;
    if (nl >= 0) {
      rest = rest.substring(nl + 1);
    } else {
      rest = "";
    }
    segment.trim();
    while (segment.length() > 0) {
      if (y > yMax) break;
      int n = segment.length();
      int bi = 0;
      int lastFit = 0;
      while (bi < n) {
        int adv = utf8FirstCharLen((uint8_t)segment[bi]);
        if (bi + adv > n) adv = n - bi;
        String trial = segment.substring(0, bi + adv);
        if (u8g2.getUTF8Width(trial.c_str()) <= maxW) {
          lastFit = bi + adv;
          bi += adv;
        } else {
          break;
        }
      }
      if (lastFit == 0) {
        int adv = utf8FirstCharLen((uint8_t)segment[0]);
        if (adv > n) adv = n;
        lastFit = adv;
      }
      String line = segment.substring(0, lastFit);
      segment = segment.substring(lastFit);
      segment.trim();
      u8g2.setForegroundColor(ST77XX_WHITE);
      u8g2.setCursor(x, y);
      u8g2.print(line);
      y += lineStep;
    }
  }
}

void drawSettingsSoftwareUpdate(int selected, const char* curVer, const char* latestVer, bool available, const char* hint,
                                const char* updateNotes) {
  drawSettingsHeader("软件更新");

  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(ST77XX_WHITE);
  // 右上角：当前版本
  {
    const char* v = curVer ? curVer : CURRENT_VERSION;
    String vv = String("V") + v;
    int w = u8g2.getUTF8Width(vv.c_str());
    u8g2.setForegroundColor(0xAD55);
    u8g2.setCursor(240 - 10 - w, 24);
    u8g2.print(vv);
  }

  const bool showDetail = updateNotes && updateNotes[0] && (!hint || !hint[0]);

  int bodyStartY = 86;

  if (!hint || !hint[0]) {
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    if (available && latestVer && latestVer[0]) {
      String head = String("发现更新，可升级  V") + latestVer;
      u8g2.setForegroundColor(ST77XX_GREEN);
      if (u8g2.getUTF8Width(head.c_str()) <= 220) {
        u8g2.setCursor(10, 66);
        u8g2.print(head);
        bodyStartY = 86;
      } else {
        u8g2.setCursor(10, 66);
        u8g2.print("发现更新，可升级");
        String vOnly = String("V") + latestVer;
        u8g2.setForegroundColor(ST77XX_GREEN);
        u8g2.setCursor(10, 86);
        u8g2.print(vOnly);
        bodyStartY = 106;
      }
    } else {
      u8g2.setForegroundColor(0xAD55);
      u8g2.setCursor(10, 66);
      u8g2.print("当前已是最新版本");
      bodyStartY = 86;
    }
  } else {
    u8g2.setForegroundColor(0xAD55);
    u8g2.setCursor(10, 66);
    u8g2.print(hint);
  }

  if (showDetail) {
    int bodyY = bodyStartY;
    drawSoftwareUpdateNotesWrapped(10, bodyY, 158, updateNotes);
  }

  // 选项：无更新时仅“返回”；有更新时“返回/开始更新”
  if (!available) {
    // 仅返回：不展示按钮，仅提示“短按返回”
    (void)selected;
    tft.fillRect(0, 214, 240, 26, ST77XX_BLACK);
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setFontMode(1);
    u8g2.setForegroundColor(0xAD55);
    u8g2.setCursor(10, 236);
    u8g2.print("短按返回");
  } else {
    if (selected < 0) selected = 0;
    if (selected > 1) selected = 1;
    // 与主设置菜单一致的行距，避免两枚按钮挤在一起
    drawMenuItem(168, "返回", selected == 0);
    drawMenuItem(198, "开始更新", selected == 1);
    drawBottomHint("短按切换", "长按执行");
  }
}

void drawSettingsAbout(int selected) {
  drawSettingsHeader("关于本机");

  // 右上角：版本号
  u8g2.setFont(u8g2_font_wqy12_t_chinese3);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(0xAD55);
  {
    const String v = String("v") + String(CURRENT_VERSION);
    int w = u8g2.getUTF8Width(v.c_str());
    u8g2.setCursor(240 - 10 - w, 24);
    u8g2.print(v);
  }

  // 存储信息
  size_t total = LittleFS.totalBytes();
  size_t used = LittleFS.usedBytes();

  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setFontMode(1);

  u8g2.setForegroundColor(ST77XX_ORANGE);
  u8g2.setCursor(10, 66);
  u8g2.print("存储信息");
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(10, 90);
  u8g2.print("已用 ");
  u8g2.print(String(used / 1024));
  u8g2.print("KB / ");
  u8g2.print(String(total / 1024));
  u8g2.print("KB");

  // 控制板信息
  u8g2.setForegroundColor(ST77XX_ORANGE);
  u8g2.setCursor(10, 114);
  u8g2.print("控制板信息");
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(10, 138);
  u8g2.print(ESP.getChipModel());
  u8g2.print(" rev");
  u8g2.print(ESP.getChipRevision());

  // 屏幕信息
  u8g2.setForegroundColor(ST77XX_ORANGE);
  u8g2.setCursor(10, 162);
  u8g2.print("屏幕信息");
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(10, 186);
  u8g2.print("ST7789 240x240");

  // 左下角提示：短按返回
  tft.fillRect(0, 214, 240, 26, ST77XX_BLACK);
  u8g2.setForegroundColor(0xAD55);
  u8g2.setCursor(10, 236);
  u8g2.print("短按返回");
}

static bool g_eraseHoldStaticDrawn = false;
/** 进度条已填充宽度；<0 表示尚未初始化条内区域 */
static int g_eraseHoldLastFillW = -1;
/** 上次已绘制的显示用千分比（分桶+节流，避免文字狂刷闪烁） */
static int g_eraseHoldLastDisplayTenths = -1;

void drawHoldProgressReset() {
  g_eraseHoldStaticDrawn = false;
  g_eraseHoldLastFillW = -1;
  g_eraseHoldLastDisplayTenths = -1;
  invalidateSettingsMenuLayout();
}

void drawSettingsEraseHoldProgress(uint32_t elapsedMs, uint32_t totalMs) {
  if (totalMs == 0) totalMs = 1;

  if (!g_eraseHoldStaticDrawn) {
    g_eraseHoldStaticDrawn = true;
    tft.fillScreen(ST77XX_BLACK);
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setFontMode(1);

    u8g2.setForegroundColor(ST77XX_ORANGE);
    u8g2.setCursor(10, 22);
    u8g2.print("抹掉所有内容和设置");

    u8g2.setForegroundColor(0xAD55);
    u8g2.setCursor(10, 42);
    u8g2.print("将清除以下项：");

    u8g2.setForegroundColor(ST77XX_WHITE);
    u8g2.setCursor(10, 62);
    u8g2.print("· WiFi 与已保存密码");
    u8g2.setCursor(10, 82);
    u8g2.print("· 图片、笔记与缓存");
    u8g2.setCursor(10, 102);
    u8g2.print("· 轮播、模式与亮度等");

    tft.drawRect(10, 128, 220, 16, 0xFFFF);
    // 条内先铺黑，之后只向右增量铺绿，避免每帧整段清屏闪烁
    tft.fillRect(11, 129, 218, 14, ST77XX_BLACK);

    u8g2.setForegroundColor(0xAD55);
    u8g2.setCursor(10, 228);
    u8g2.print("松开取消");

    g_eraseHoldLastFillW = 0;
    g_eraseHoldLastDisplayTenths = -1;
  }

  uint32_t e = elapsedMs;
  if (e > totalMs) e = totalMs;
  const int fillW = (int)((uint32_t)218 * e / totalMs);

  // 增量绘制进度条（仅扩展或回缩差值，不整段清空）
  if (fillW != g_eraseHoldLastFillW) {
    if (fillW > g_eraseHoldLastFillW) {
      const int x0 = 11 + g_eraseHoldLastFillW;
      const int w = fillW - g_eraseHoldLastFillW;
      if (w > 0) tft.fillRect(x0, 129, w, 14, 0x07E0);
    } else if (fillW < g_eraseHoldLastFillW) {
      const int x0 = 11 + fillW;
      const int w = g_eraseHoldLastFillW - fillW;
      if (w > 0) tft.fillRect(x0, 129, w, 14, ST77XX_BLACK);
    }
    g_eraseHoldLastFillW = fillW;
  }

  // 百分比：按时间分桶（约 100ms 一变），且只擦除文字条带，减轻全宽黑块闪烁
  constexpr uint32_t kPercentBucketMs = 100;
  uint32_t eb = (elapsedMs / kPercentBucketMs) * kPercentBucketMs;
  if (eb > totalMs) eb = totalMs;
  int displayTenths = (int)((uint32_t)1000 * eb / totalMs);
  if (elapsedMs >= totalMs) displayTenths = 1000;

  if (displayTenths != g_eraseHoldLastDisplayTenths) {
    tft.fillRect(8, 152, 136, 24, ST77XX_BLACK);
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setFontMode(1);
    u8g2.setForegroundColor(ST77XX_CYAN);
    u8g2.setCursor(10, 170);
    u8g2.print("进度 ");
    u8g2.print(displayTenths / 10);
    u8g2.print(".");
    u8g2.print(displayTenths % 10);
    u8g2.print("%");
    g_eraseHoldLastDisplayTenths = displayTenths;
  }
}

