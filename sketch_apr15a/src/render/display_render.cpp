#include "include/globals.h"
#include "include/render/display_render.h"
#include "include/render/expression_mode.h"

// forward decls from other modules
void loadSavedImage();

void drawClockFace() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  tft.fillScreen(ST77XX_BLACK);

  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(5);
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((240 - w) / 2, 75);
  tft.print(timeStr);

  char dateStr[32];
  sprintf(dateStr, "%04d年%02d月%02d日", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setForegroundColor(ST77XX_WHITE);
  int dw = u8g2.getUTF8Width(dateStr);
  u8g2.setCursor((240 - dw) / 2, 156);
  u8g2.print(dateStr);

  const char* weekdays[] = {"星期日","星期一","星期二","星期三","星期四","星期五","星期六"};
  u8g2.setForegroundColor(ST77XX_ORANGE);
  int ww = u8g2.getUTF8Width(weekdays[timeinfo.tm_wday]);
  u8g2.setCursor((240 - ww) / 2, 196);
  u8g2.print(weekdays[timeinfo.tm_wday]);
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

void refreshDisplayByMode() {
  if (displayMode == 0) loadSavedImage();
  else if (displayMode == 1) drawClockFace();
  else if (displayMode == 2) displayNoteOnScreen();
  else if (displayMode == 3) expressionModeEnter();
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

void drawBootSplash() {
  // 暗色底 + 橙色像素机器人 + Hello Cody
  // 选用较暗的中性深灰（比纯黑更柔和）
  const uint16_t bgDark = tft.color565(28, 28, 31); // ~ #1c1c1f
  tft.fillScreen(bgDark);

  // 机器人 + 文案：整体居中
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

  const int gap = 6; // logo 与字的间距（越小越贴近）
  const int groupH = botH + gap + (int)h;
  const int topY = (240 - groupH) / 2;

  drawClaudeBotPixelArt(120, topY, s, ST77XX_BLACK);

  // “加粗”效果：叠印两次（向右偏移 1px）
  int textX = (240 - (int)w) / 2;
  int textY = topY + botH + gap;
  tft.setCursor(textX, textY);
  tft.print(msg);
  tft.setCursor(textX + 1, textY);
  tft.print(msg);
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

void drawSystemInfoPage() {
  tft.fillScreen(ST77XX_BLACK);

  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setForegroundColor(ST77XX_CYAN);
  u8g2.setCursor(10, 24);
  u8g2.print("系统信息");
  u8g2.setForegroundColor(0xAD55);
  u8g2.setCursor(140, 24);
  u8g2.print("V");
  u8g2.print(CURRENT_VERSION);

  tft.drawFastHLine(10, 32, 220, 0x4208);

  const bool connected = (WiFi.status() == WL_CONNECTED);
  const String ssid = connected ? WiFi.SSID() : String("未连接");
  const String ip = connected ? WiFi.localIP().toString() : String("--");

  size_t total = LittleFS.totalBytes();
  size_t used = LittleFS.usedBytes();
  size_t free = (total > used) ? (total - used) : 0;

  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(10, 52);
  u8g2.print("WiFi");
  u8g2.setForegroundColor(connected ? ST77XX_GREEN : ST77XX_RED);
  u8g2.setCursor(70, 52);
  u8g2.print(connected ? "已连接" : "未连接");

  u8g2.setForegroundColor(ST77XX_ORANGE);
  u8g2.setCursor(10, 76);
  u8g2.print("SSID:");
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(70, 76);
  u8g2.print(ssid);

  u8g2.setForegroundColor(ST77XX_ORANGE);
  u8g2.setCursor(10, 100);
  u8g2.print("IP:");
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(70, 100);
  u8g2.print(ip);

  u8g2.setForegroundColor(ST77XX_ORANGE);
  u8g2.setCursor(10, 124);
  u8g2.print("FS:");
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(70, 124);
  u8g2.print(String(used / 1024));
  u8g2.print("/");
  u8g2.print(String(total / 1024));
  u8g2.print("KB");

  tft.drawFastHLine(10, 140, 220, 0x4208);

  u8g2.setForegroundColor(ST77XX_YELLOW);
  u8g2.setCursor(10, 162);
  u8g2.print("可用: ");
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.print(String(free / 1024));
  u8g2.print(" KB");

  // 底部提示
  u8g2.setForegroundColor(0xAD55);
  u8g2.setCursor(10, 238);
  u8g2.print("短按返回  长按格式化");
}

void drawHoldProgress(const char* title, const char* hint, int secondsHeld, int totalSeconds) {
  // secondsHeld 从 0 递增到 totalSeconds
  if (secondsHeld < 0) secondsHeld = 0;
  if (secondsHeld > totalSeconds) secondsHeld = totalSeconds;

  // 降闪烁：只在标题变化/首次进入时整屏绘制，其余仅局部刷新
  static String lastTitle = "";
  static int lastPct = -1;
  static int lastRemain = -1;
  static bool frameDrawn = false;

  String ttl = title ? String(title) : String("长按");
  if (!frameDrawn || ttl != lastTitle || secondsHeld == 0) {
    frameDrawn = true;
    lastTitle = ttl;
    lastPct = -1;
    lastRemain = -1;

    tft.fillScreen(ST77XX_BLACK);
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);

    u8g2.setForegroundColor(ST77XX_ORANGE);
    u8g2.setCursor(10, 60);
    u8g2.print(ttl);

    // 进度条框架
    tft.drawRect(10, 130, 220, 16, 0xFFFF);

    u8g2.setForegroundColor(0xAD55);
    u8g2.setCursor(10, 230);
    u8g2.print("松开取消");
  }

  u8g2.setFont(u8g2_font_wqy16_t_gb2312);

  // 更新提示行（清理区域再重绘）
  tft.fillRect(0, 74, 240, 32, ST77XX_BLACK);
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(10, 92);
  if (secondsHeld < 2) {
    u8g2.print(hint ? hint : "继续按住...");
  } else {
    int remain = totalSeconds - secondsHeld;
    if (remain != lastRemain) lastRemain = remain;
    u8g2.print("剩余 ");
    u8g2.print(remain);
    u8g2.print(" 秒");
  }

  // 更新进度条与百分比（局部刷新）
  if (secondsHeld >= 2) {
    int p = (secondsHeld * 100) / totalSeconds;
    if (p != lastPct) {
      lastPct = p;
      // 清空条内区域
      tft.fillRect(11, 131, 218, 14, ST77XX_BLACK);
      int fill = (218 * p) / 100;
      if (fill < 0) fill = 0;
      if (fill > 218) fill = 218;
      tft.fillRect(11, 131, fill, 14, 0x07E0);

      tft.fillRect(0, 154, 240, 24, ST77XX_BLACK);
      u8g2.setForegroundColor(ST77XX_CYAN);
      u8g2.setCursor(10, 170);
      u8g2.print("进度 ");
      u8g2.print(p);
      u8g2.print("%");
    }
  } else {
    // 未到 2 秒：清空条内与百分比区
    tft.fillRect(11, 131, 218, 14, ST77XX_BLACK);
    tft.fillRect(0, 154, 240, 24, ST77XX_BLACK);
    lastPct = -1;
  }
}

