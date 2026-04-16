#include "include/globals.h"
#include "include/render/expression_mode.h"

#include <math.h>

// 说明：
// - 从你提供的“多表情动画”代码中抽取核心：差分眼睛刷新 + 若干表情动作
// - 为避免阻塞 WebServer/串口，这里全部用 millis() 做非阻塞 step 状态机（无 delay）

static const int16_t DISP_W = 240;
static const int16_t DISP_H = 240;

static const int16_t EYE_W = 30;
static const int16_t EYE_GAP = 120;
static const int16_t EYE_CY = 85;

static uint16_t C_ORANGE, C_PINK, C_BLUE, C_YELLOW, C_GREEN, C_RED, C_BROWN, C_CYAN, C_GRAY;
static const uint16_t C_WHITE = ST77XX_WHITE;
static const uint16_t C_BLACK = ST77XX_BLACK;

static bool inited = false;
static bool busy = false;
static bool lastBlush = false;

static int16_t lastLX = 0, lastRX = 0;
static int16_t lastLY = 0, lastRY = 0;
static int16_t lastLH = 60, lastRH = 60;

static unsigned long lastSleepTime = 0;
static const unsigned long SLEEP_COOLDOWN = 600000;

static unsigned long nextActionTime = 0;
static unsigned long stepDueAt = 0;
static int currentAction = -1;
static int stepIdx = 0;

static void initColoursOnce() {
  if (inited) return;
  C_ORANGE = tft.color565(218, 17, 0);
  C_PINK = tft.color565(255, 180, 180);
  C_BLUE = tft.color565(50, 150, 255);
  C_YELLOW = tft.color565(255, 220, 0);
  C_GREEN = tft.color565(50, 200, 80);
  C_RED = tft.color565(255, 50, 50);
  C_BROWN = tft.color565(130, 80, 40);
  C_CYAN = tft.color565(0, 255, 255);
  C_GRAY = tft.color565(150, 150, 150);
  inited = true;
}

static inline int16_t eyeLX(int16_t ox) { return (DISP_W - (EYE_W * 2 + EYE_GAP)) / 2 + ox; }
static inline int16_t eyeRX(int16_t ox) { return eyeLX(ox) + EYE_W + EYE_GAP; }

static void drawBlush() {
  tft.fillCircle(eyeLX(0) - 15, EYE_CY + 55, 10, C_PINK);
  tft.fillCircle(eyeRX(0) + 45, EYE_CY + 55, 10, C_PINK);
}

static void clearProps() {
  tft.fillScreen(C_ORANGE);
  lastLH = 0;
  lastRH = 0;
  lastBlush = false;
}

static void diffUpdateRect(int16_t oL, int16_t oT, int16_t oW, int16_t oH,
                           int16_t nL, int16_t nT, int16_t nW, int16_t nH) {
  oL -= 1;
  oT -= 1;
  oW += 2;
  oH += 2;
  int16_t oR = oL + oW;
  int16_t oB = oT + oH;
  int16_t nR = nL + nW;
  int16_t nB = nT + nH;

  tft.fillRect(nL, nT, nW, nH, C_BLACK);

  if (oT < nT) tft.fillRect(oL, oT, oW, min(nT, oB) - oT, C_ORANGE);
  if (oB > nB) tft.fillRect(oL, max(nB, oT), oW, oB - max(nB, oT), C_ORANGE);

  int16_t y_start = max(oT, nT);
  int16_t y_end = min(oB, nB);
  if (y_start < y_end) {
    if (oL < nL) tft.fillRect(oL, y_start, min(nL, oR) - oL, y_end - y_start, C_ORANGE);
    if (oR > nR) tft.fillRect(max(nR, oL), y_start, oR - max(nR, oL), y_end - y_start, C_ORANGE);
  }
}

static void drawEyesSmartEx(int16_t lox, int16_t loy, int16_t lh, int16_t rox, int16_t roy, int16_t rh, bool showBlush) {
  int16_t oldLX = eyeLX(lastLX);
  int16_t oldLY = lastLY - lastLH / 2;
  int16_t oldRX = eyeRX(lastRX);
  int16_t oldRY = lastRY - lastRH / 2;
  int16_t newLX = eyeLX(lox);
  int16_t newLY = loy - lh / 2;
  int16_t newRX = eyeRX(rox);
  int16_t newRY = roy - rh / 2;

  if (lastBlush && !showBlush) {
    tft.fillCircle(eyeLX(0) - 15, EYE_CY + 55, 10, C_ORANGE);
    tft.fillCircle(eyeRX(0) + 45, EYE_CY + 55, 10, C_ORANGE);
  } else if (!lastBlush && showBlush) {
    drawBlush();
  }

  diffUpdateRect(oldLX, oldLY, EYE_W, lastLH, newLX, newLY, EYE_W, lh);
  diffUpdateRect(oldRX, oldRY, EYE_W, lastRH, newRX, newRY, EYE_W, rh);

  lastLX = lox;
  lastRX = rox;
  lastLY = loy;
  lastRY = roy;
  lastLH = lh;
  lastRH = rh;
  lastBlush = showBlush;
}

static void drawEyesSmart(int16_t ox, int16_t h, bool showBlush) {
  drawEyesSmartEx(ox, EYE_CY, h, ox, EYE_CY, h, showBlush);
}

enum ActionId {
  // 对齐你给的 case 0..30（基础表情 + 道具互动）
  ACT_BLINK = 0,
  ACT_HAPPY = 1,
  ACT_PLEADING = 2,
  ACT_LOOK_AROUND = 3,
  ACT_WINK_RIGHT = 4,
  ACT_DIZZY = 5,
  ACT_WINK_LEFT = 6,
  ACT_SHY_LOOK = 7,
  ACT_THINKING = 8,
  ACT_NOTICE = 9,
  ACT_SLEEPY = 10,
  ACT_CONFUSED = 11,
  ACT_EXCITED = 12,
  ACT_SNOBBY = 13,
  ACT_SIDE_TILT = 14,
  ACT_SLEEPING = 15,

  ACT_SINGING = 16,
  ACT_WINDMILL = 17,
  ACT_FLOWER = 18,
  ACT_BUBBLES = 19,
  ACT_READING = 20,
  ACT_EATING = 21,
  ACT_GAMING = 22,
  ACT_WORKOUT = 23,
  ACT_CAMERA = 24,
  ACT_MAGIC = 25,
  ACT_COFFEE = 26,
  ACT_PAINTING = 27,
  ACT_FISHING = 28,
  ACT_PARTY = 29,
  ACT_MUSIC = 30,

  // 额外经典可爱动作（31..40）
  ACT_LOVE = 31,        // 爱心暴击
  ACT_LAUGH = 32,       // 大笑
  ACT_ANGRY = 33,       // 生气
  ACT_CRY = 34,         // 呜呜哭
  ACT_SPARKLE = 35,     // 眼里有光
  ACT_WAVE = 36,        // 挥手打招呼
  ACT_THUMBS_UP = 37,   // 点赞
  ACT_SURPRISED = 38,   // 震惊（更夸张）
  ACT_SLEEP_MASK = 39,  // 眼罩睡
  ACT_HEART_RAIN = 40,  // 爱心雨

  ACT_COUNT = 41
};

static void startAction(int a) {
  busy = true;
  currentAction = a;
  stepIdx = 0;
  stepDueAt = millis();
}

static void endAction() {
  busy = false;
  currentAction = -1;
  stepIdx = 0;
  stepDueAt = 0;
  nextActionTime = millis() + (unsigned long)random(2000, 5000);
}

static void drawGamingPropsOnce() {
  // Switch 掌机（简化版，来自原代码）
  tft.fillRoundRect(50, 155, 35, 45, 8, C_CYAN);
  tft.fillRect(75, 155, 10, 45, C_CYAN);
  tft.fillCircle(65, 168, 5, C_BLACK);
  tft.fillRect(63, 180, 4, 10, C_BLACK);
  tft.fillRect(60, 183, 10, 4, C_BLACK);

  tft.fillRoundRect(155, 155, 35, 45, 8, C_RED);
  tft.fillRect(155, 155, 10, 45, C_RED);
  tft.fillCircle(175, 185, 5, C_BLACK);
  tft.fillCircle(175, 163, 3, C_BLACK);
  tft.fillCircle(168, 170, 3, C_BLACK);
  tft.fillCircle(182, 170, 3, C_BLACK);
  tft.fillCircle(175, 177, 3, C_BLACK);

  tft.fillRect(85, 155, 70, 45, C_BLACK);
  tft.fillRect(88, 158, 64, 39, C_GRAY);
}

static void clearTextLine() {
  tft.fillRect(0, 205, 240, 30, C_ORANGE);
}

static void showBottomText(const char* msg) {
  clearTextLine();
  // 使用项目内置的 U8g2 中文字体，避免乱码
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setForegroundColor(C_WHITE);
  u8g2.setBackgroundColor(C_ORANGE);
  u8g2.setFontMode(0); // 实心背景，避免黑色残影
  // y 为基线，区域高度 30，取 228 更贴合底部
  u8g2.setCursor(10, 228);
  u8g2.print(msg);
  u8g2.setFontMode(1); // 还原透明模式，避免影响其它界面
}

static void showTopRightStageText(const char* msg) {
  // 右上角阶段文字（避免遮挡主体）
  tft.fillRect(146, 4, 92, 26, C_ORANGE);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setForegroundColor(C_WHITE);
  u8g2.setBackgroundColor(C_ORANGE);
  u8g2.setFontMode(0); // 实心背景，避免黑色残影
  u8g2.setCursor(148, 22);
  u8g2.print(msg);
  u8g2.setFontMode(1);
}

static void drawMicOnce() {
  // 简化麦克风（右侧）
  tft.fillRoundRect(160, 160, 16, 40, 8, C_GRAY);
  tft.fillCircle(168, 150, 15, C_BLACK);
}

static void drawWindmillFrame(bool cross) {
  // 风车杆
  tft.fillRect(60, 150, 4, 60, C_BROWN);
  // 擦除叶片区域后重画（避免叠色）
  tft.fillCircle(62, 150, 25, C_ORANGE);
  if (cross) {
    tft.drawLine(48, 136, 76, 164, C_YELLOW);
    tft.drawLine(48, 164, 76, 136, C_RED);
  } else {
    tft.fillRect(42, 148, 40, 4, C_YELLOW);
    tft.fillRect(60, 130, 4, 40, C_RED);
  }
}

static void drawFlowerOnce() {
  tft.fillRect(120, 170, 4, 40, C_GREEN);
  tft.fillCircle(122, 160, 10, C_YELLOW);
  tft.fillCircle(110, 160, 8, C_RED);
  tft.fillCircle(134, 160, 8, C_RED);
  tft.fillCircle(122, 148, 8, C_RED);
  tft.fillCircle(122, 172, 8, C_RED);
}

static void drawBookOnce() {
  tft.fillRect(70, 160, 100, 40, C_WHITE);
  tft.drawLine(120, 160, 120, 200, C_GRAY);
  tft.setTextColor(C_BLACK);
  tft.setTextSize(1);
  tft.setCursor(80, 172);
  tft.print("E=mc^2"); // 公式保留
}

static void drawCookieOnce() {
  tft.fillCircle(120, 170, 25, C_BROWN);
  tft.fillCircle(120, 170, 10, C_ORANGE);
}

static void drawWorkoutFrame(int y) {
  // 杠铃
  tft.fillRect(40, 120, 160, 80, C_ORANGE);
  tft.fillRect(60, y, 120, 6, C_GRAY);
  tft.fillRect(50, y - 15, 20, 36, C_BLACK);
  tft.fillRect(170, y - 15, 20, 36, C_BLACK);
}

static void drawCameraOnce() {
  tft.fillRoundRect(80, 150, 80, 50, 5, C_BLUE);
  tft.fillCircle(120, 175, 18, C_BLACK);
  tft.fillCircle(120, 175, 10, C_WHITE);
}

static void drawMagicWandOnce() {
  tft.drawLine(100, 200, 140, 150, C_BROWN);
  tft.drawLine(101, 200, 141, 150, C_BROWN);
  tft.fillCircle(140, 150, 8, C_YELLOW);
}

static void drawCoffeeOnce() {
  tft.fillRect(100, 160, 40, 40, C_WHITE);
  tft.drawCircle(140, 180, 12, C_WHITE);
  tft.fillCircle(140, 180, 8, C_ORANGE);
}

static void drawPaletteOnce() {
  tft.fillCircle(80, 180, 25, C_BROWN);
  tft.fillCircle(70, 180, 6, C_ORANGE);
  tft.fillCircle(85, 170, 5, C_RED);
  tft.fillCircle(90, 185, 5, C_BLUE);
  tft.fillCircle(75, 195, 5, C_YELLOW);
}

static void drawHeadphonesFrame(bool on) {
  // 简化耳机（左右耳罩 + 圆弧）
  uint16_t col = on ? C_GRAY : C_ORANGE;
  tft.fillRect(30, EYE_CY - 20, 15, 40, col);
  tft.fillRect(195, EYE_CY - 20, 15, 40, col);
  tft.drawCircle(120, EYE_CY + 10, 95, col);
  tft.drawCircle(120, EYE_CY + 10, 94, col);
}

static void drawHeart(int cx, int cy, int r, uint16_t col) {
  // 简单爱心：两个圆 + 三角
  tft.fillCircle(cx - r, cy, r, col);
  tft.fillCircle(cx + r, cy, r, col);
  tft.fillTriangle(cx - 2 * r, cy, cx + 2 * r, cy, cx, cy + 3 * r, col);
}

static void drawThumbOnce(int x, int y, uint16_t col) {
  // 简化点赞手（像素块风）
  tft.fillRoundRect(x, y, 34, 26, 6, col);      // 手掌
  tft.fillRoundRect(x + 22, y - 14, 12, 20, 5, col); // 大拇指
  tft.fillRect(x + 4, y + 6, 22, 4, C_BLACK);   // 简单分割线
}

static void drawWaveHandFrame(int x, int y, bool up, uint16_t col) {
  // 简化挥手（两帧上下摆）
  int dy = up ? -6 : 6;
  tft.fillRoundRect(x, y + dy, 18, 28, 6, col);
  tft.fillCircle(x + 9, y + dy - 6, 8, col);
}

static void drawSparkle(int x, int y, int s, uint16_t col) {
  tft.drawLine(x - s, y, x + s, y, col);
  tft.drawLine(x, y - s, x, y + s, col);
  tft.drawLine(x - s + 1, y - s + 1, x + s - 1, y + s - 1, col);
  tft.drawLine(x - s + 1, y + s - 1, x + s - 1, y - s + 1, col);
}

static void partyConfettiStep(int i) {
  // 少量彩纸，避免大量闪烁
  uint16_t c = (i % 3 == 0) ? C_RED : ((i % 3 == 1) ? C_CYAN : C_GREEN);
  int x = 20 + (int)random(0, 200);
  int y = 110 + (int)random(0, 80);
  tft.fillRect(x, y, 6, 6, c);
}

static void fishingDrawStatic() {
  // 静态：只画水面（鱼竿/鱼线由动画阶段驱动）
  tft.fillRect(0, 180, 240, 60, C_BLUE);
}

static int old_fx = 120;
static int old_fy = 205;
static float fish_fy = 205.0f;
static int old_bobY = 180;
static int old_bobX = 120;
static int old_line_x1 = 120, old_line_y1 = 140, old_line_x2 = 120, old_line_y2 = 180;
static bool fishingBite = false;
static unsigned long fishingBiteUntil = 0;
static unsigned long fishingNextBiteAt = 0;
static bool fishingRodRaised = false;
static bool old_fishVertical = false;
static int old_rod_x1 = 40, old_rod_y1 = 175, old_rod_x2 = 120, old_rod_y2 = 140;
static int old_fish_w = 56;
static int old_fish_h = 32;

static inline int fishingRodBaseY() { return fishingRodRaised ? 155 : 175; }
static inline int fishingRodTipY() { return fishingRodRaised ? 118 : 140; }
static inline int fishingRodBaseX() { return 40; }
static inline int fishingRodTipX() { return 120; }

static void fishingRestoreRect(int x, int y, int w, int h);

static void fishingDrawRod(int x1, int y1, int x2, int y2) {
  // 加粗鱼竿：叠画几条平行线（避免不清晰）
  tft.drawLine(x1, y1, x2, y2, C_BROWN);
  tft.drawLine(x1 + 1, y1, x2 + 1, y2, C_BROWN);
  tft.drawLine(x1, y1 + 1, x2, y2 + 1, C_BROWN);
}

static void fishingRestoreRodBBox(int x1, int y1, int x2, int y2) {
  int rx0 = min(x1, x2) - 2;
  int ry0 = min(y1, y2) - 2;
  int rx1 = max(x1, x2) + 2;
  int ry1 = max(y1, y2) + 2;
  fishingRestoreRect(rx0, ry0, rx1 - rx0, ry1 - ry0);
}

static void fishingRestoreRect(int x, int y, int w, int h) {
  // 只恢复这块区域的背景（橙色天空 + 蓝色水面）
  int x0 = max(0, x);
  int y0 = max(0, y);
  int x1 = min((int)DISP_W, x + w);
  int y1 = min((int)DISP_H, y + h);
  if (x1 <= x0 || y1 <= y0) return;

  // 上半部：橙色
  if (y0 < 180) {
    int hy1 = min(y1, 180);
    tft.fillRect(x0, y0, x1 - x0, hy1 - y0, C_ORANGE);
  }
  // 水面：蓝色
  if (y1 > 180) {
    int wy0 = max(y0, 180);
    tft.fillRect(x0, wy0, x1 - x0, y1 - wy0, C_BLUE);
  }
}

static void fishingDrawBobber(int bobY) {
  // 浮漂（在水面附近上下轻微抖动）
  tft.fillCircle(120, bobY, 6, C_RED);
  // 小高光
  tft.fillCircle(118, bobY - 2, 2, C_WHITE);
}

static void fishingDrawBobberXY(int bobX, int bobY) {
  tft.fillCircle(bobX, bobY, 6, C_RED);
  tft.fillCircle(bobX - 2, bobY - 2, 2, C_WHITE);
}

static void fishingDrawFishUnified(int x, int y, bool vertical, float phaseT) {
  // 同一条鱼的两种方向（横/竖旋转），保持一致的“长相/比例”
  const int bodyW = 24;
  const int bodyH = 16;
  const int w = vertical ? bodyH : bodyW;
  const int h = vertical ? bodyW : bodyH;

  tft.fillRoundRect(x - w / 2, y - h / 2, w, h, 5, C_CYAN);

  if (!vertical) {
    bool faceRight = cosf(phaseT) > 0;
    if (faceRight) {
      tft.fillTriangle(x - w / 2, y, x - w / 2 - 12, y - 10, x - w / 2 - 12, y + 10, C_CYAN);
      tft.fillCircle(x + 6, y - 2, 3, C_BLACK);
      tft.fillCircle(x + 7, y - 3, 1, C_WHITE);
    } else {
      tft.fillTriangle(x + w / 2, y, x + w / 2 + 12, y - 10, x + w / 2 + 12, y + 10, C_CYAN);
      tft.fillCircle(x - 6, y - 2, 3, C_BLACK);
      tft.fillCircle(x - 7, y - 3, 1, C_WHITE);
    }
  } else {
    // 竖向：头朝上，尾在下（与横向同一条鱼）
    tft.fillTriangle(x, y - h / 2 - 10, x - 10, y - h / 2 + 2, x + 10, y - h / 2 + 2, C_CYAN);
    tft.fillTriangle(x, y + h / 2 + 10, x - 10, y + h / 2 - 2, x + 10, y + h / 2 - 2, C_CYAN);
    tft.fillCircle(x - 2, y - 6, 3, C_BLACK);
    tft.fillCircle(x - 1, y - 7, 1, C_WHITE);
  }
}

static void actionStep(unsigned long now) {
  if (now < stepDueAt) return;

  switch (currentAction) {
    case ACT_BLINK: {
      // heights: 40,20,4,20,40,60
      static const int hSeq[] = {40, 20, 4, 20, 40, 60};
      static const int dSeq[] = {25, 25, 100, 25, 25, 0};
      if (stepIdx >= 6) {
        endAction();
        return;
      }
      drawEyesSmart(0, hSeq[stepIdx], false);
      stepDueAt = now + (unsigned long)dSeq[stepIdx];
      stepIdx++;
      return;
    }
    case ACT_HAPPY: {
      if (stepIdx == 0) {
        drawEyesSmart(0, 30, true);
        stepDueAt = now + 1200;
        stepIdx = 1;
        return;
      }
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_PLEADING: {
      if (stepIdx == 0) {
        drawEyesSmart(0, 75, true);
        stepDueAt = now + 1300;
        stepIdx = 1;
        return;
      }
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_LOOK_AROUND: {
      static const int oxSeq[] = {-20, 20, 0};
      if (stepIdx >= 3) {
        endAction();
        return;
      }
      drawEyesSmart(oxSeq[stepIdx], 60, false);
      stepDueAt = now + 800;
      stepIdx++;
      return;
    }
    case ACT_WINK_LEFT: {
      if (stepIdx == 0) {
        drawEyesSmartEx(0, EYE_CY, 6, 0, EYE_CY, 60, true);
        stepDueAt = now + 1000;
        stepIdx = 1;
        return;
      }
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_WINK_RIGHT: {
      if (stepIdx == 0) {
        drawEyesSmartEx(0, EYE_CY, 60, 0, EYE_CY, 6, true);
        stepDueAt = now + 1000;
        stepIdx = 1;
        return;
      }
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_DIZZY: {
      if (stepIdx < 6) {
        bool odd = (stepIdx % 2) == 1;
        drawEyesSmartEx(0, odd ? (int16_t)(EYE_CY + 15) : (int16_t)(EYE_CY - 15), 60,
                        0, odd ? (int16_t)(EYE_CY - 15) : (int16_t)(EYE_CY + 15), 60, false);
        stepDueAt = now + 200;
        stepIdx++;
        return;
      }
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_SHY_LOOK: {
      // 害羞侧看
      if (stepIdx == 0) {
        drawEyesSmart(20, 60, true);
        stepDueAt = now + 900;
        stepIdx = 1;
        return;
      }
      if (stepIdx == 1) {
        drawEyesSmart(20, 30, true);
        stepDueAt = now + 260;
        stepIdx = 2;
        return;
      }
      if (stepIdx == 2) {
        drawEyesSmart(20, 60, true);
        stepDueAt = now + 650;
        stepIdx = 3;
        return;
      }
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_THINKING: {
      // 上移思考（平滑分段）
      if (stepIdx < 10) {
        int yoff = -(stepIdx * 2);
        drawEyesSmartEx(0, (int16_t)(EYE_CY + yoff), 60, 0, (int16_t)(EYE_CY + yoff), 60, false);
        stepDueAt = now + 40;
        stepIdx++;
        return;
      }
      if (stepIdx == 10) {
        stepDueAt = now + 900;
        stepIdx = 11;
        return;
      }
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_NOTICE: {
      // 注意到：眯眼 -> 震惊
      if (stepIdx == 0) {
        drawEyesSmart(0, 15, false);
        stepDueAt = now + 150;
        stepIdx = 1;
        return;
      }
      if (stepIdx == 1) {
        drawEyesSmart(0, 85, true);
        stepDueAt = now + 900;
        stepIdx = 2;
        return;
      }
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_SLEEPY: {
      // 困：逐步合眼，停留，然后补一个 blink
      if (stepIdx == 0) {
        drawEyesSmart(0, 40, false);
        stepDueAt = now + 380;
        stepIdx = 1;
        return;
      }
      if (stepIdx == 1) {
        drawEyesSmart(0, 15, false);
        stepDueAt = now + 650;
        stepIdx = 2;
        return;
      }
      if (stepIdx == 2) {
        drawEyesSmart(0, 4, false);
        stepDueAt = now + 900;
        stepIdx = 3;
        return;
      }
      // 结尾小眨眼序列（复用 blink 的高度序列，缩短版）
      if (stepIdx >= 3 && stepIdx < 3 + 4) {
        static const int hSeq[] = {20, 4, 20, 60};
        static const int dSeq[] = {80, 120, 80, 0};
        int j = stepIdx - 3;
        drawEyesSmart(0, hSeq[j], false);
        stepDueAt = now + (unsigned long)dSeq[j];
        stepIdx++;
        return;
      }
      endAction();
      return;
    }
    case ACT_CONFUSED: {
      if (stepIdx == 0) {
        drawEyesSmartEx(0, EYE_CY, 40, 0, (int16_t)(EYE_CY - 5), 70, false);
        stepDueAt = now + 1200;
        stepIdx = 1;
        return;
      }
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_EXCITED: {
      if (stepIdx < 8) {
        int off = (stepIdx % 2 == 0) ? 8 : -8;
        drawEyesSmart(0, (int16_t)(60 + off), true);
        stepDueAt = now + 70;
        stepIdx++;
        return;
      }
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_SNOBBY: {
      if (stepIdx == 0) {
        drawEyesSmart(-25, 60, false);
        stepDueAt = now + 520;
        stepIdx = 1;
        return;
      }
      if (stepIdx == 1) {
        drawEyesSmart(-25, 4, false);
        stepDueAt = now + 900;
        stepIdx = 2;
        return;
      }
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_SIDE_TILT: {
      if (stepIdx == 0) {
        drawEyesSmart(-30, 60, false);
        stepDueAt = now + 520;
        stepIdx = 1;
        return;
      }
      if (stepIdx == 1) {
        drawEyesSmart(30, 60, false);
        stepDueAt = now + 520;
        stepIdx = 2;
        return;
      }
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_SINGING: {
      if (stepIdx == 0) {
        clearProps();
        drawEyesSmart(0, 60, true);
        drawMicOnce();
        showBottomText("高音咏唱中~");
        stepDueAt = now + 250;
        stepIdx = 1;
        return;
      }
      if (stepIdx < 1 + 10) {
        // 眼睛轻微跟节奏
        bool down = ((stepIdx - 1) % 2) == 0;
        drawEyesSmart(0, down ? 40 : 60, true);
        stepDueAt = now + (down ? 260 : 340);
        stepIdx++;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_WINDMILL: {
      if (stepIdx == 0) {
        clearProps();
        drawEyesSmart(-20, 60, false);
        stepDueAt = now + 60;
        stepIdx = 1;
        return;
      }
      if (stepIdx < 1 + 26) {
        drawWindmillFrame(((stepIdx - 1) % 2) == 0);
        stepDueAt = now + 110;
        stepIdx++;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_FLOWER: {
      if (stepIdx == 0) {
        clearProps();
        drawEyesSmart(0, 40, true);
        drawFlowerOnce();
        showBottomText("献上花束！");
        stepDueAt = now + 1300;
        stepIdx = 1;
        return;
      }
      if (stepIdx == 1) {
        drawEyesSmartEx(0, EYE_CY, 60, 0, EYE_CY, 6, true);
        stepDueAt = now + 900;
        stepIdx = 2;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_BUBBLES: {
      if (stepIdx == 0) {
        clearProps();
        drawEyesSmart(20, 30, false);
        // 简化：画一个泡泡棒
        tft.fillRect(50, 180, 4, 30, C_PINK);
        tft.drawCircle(52, 170, 10, C_PINK);
        stepDueAt = now + 120;
        stepIdx = 1;
        return;
      }
      if (stepIdx < 1 + 18) {
        int i = stepIdx - 1;
        int y = 170 - i * 6;
        int x = 80 + (i * 7);
        // 画泡泡
        tft.drawCircle(x, y, 8, C_CYAN);
        tft.drawCircle(x + 25, y + 10, 12, C_WHITE);
        // 过一会儿擦掉（局部）
        stepDueAt = now + 120;
        stepIdx++;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_READING: {
      if (stepIdx == 0) {
        clearProps();
        drawBookOnce();
        showBottomText("奥义研习中…");
        drawEyesSmartEx(0, (int16_t)(EYE_CY + 15), 60, 0, (int16_t)(EYE_CY + 15), 60, false);
        stepDueAt = now + 1100;
        stepIdx = 1;
        return;
      }
      if (stepIdx == 1) {
        drawEyesSmartEx(0, (int16_t)(EYE_CY + 15), 4, 0, (int16_t)(EYE_CY + 15), 4, false);
        stepDueAt = now + 160;
        stepIdx = 2;
        return;
      }
      if (stepIdx == 2) {
        drawEyesSmartEx(0, (int16_t)(EYE_CY + 15), 60, 0, (int16_t)(EYE_CY + 15), 60, false);
        stepDueAt = now + 1100;
        stepIdx = 3;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_EATING: {
      if (stepIdx == 0) {
        clearProps();
        drawEyesSmart(0, 15, true);
        drawCookieOnce();
        showBottomText("补充魔力！");
        stepDueAt = now + 700;
        stepIdx = 1;
        return;
      }
      if (stepIdx >= 1 && stepIdx <= 4) {
        // 用橙色“咬掉”不同位置
        int bx = 120, by = 170;
        if (stepIdx == 1) { bx = 105; by = 155; }
        if (stepIdx == 2) { bx = 135; by = 155; }
        if (stepIdx == 3) { bx = 120; by = 185; }
        tft.fillCircle(bx, by, 15, C_ORANGE);
        stepDueAt = now + 700;
        stepIdx++;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_GAMING: {
      if (stepIdx == 0) {
        drawGamingPropsOnce();
        showBottomText("对战启动！");
        stepDueAt = now + 80;
        stepIdx = 1;
        return;
      }
      if (stepIdx <= 8) {
        uint16_t screenColor = (stepIdx % 3 == 0) ? C_BLUE : ((stepIdx % 3 == 1) ? C_GREEN : C_YELLOW);
        tft.fillRect(88, 158, 64, 39, screenColor);
        int off = (stepIdx % 2 == 0) ? -25 : 25;
        drawEyesSmart(off, 60, false);
        stepDueAt = now + 200;
        stepIdx++;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_WORKOUT: {
      if (stepIdx == 0) {
        clearProps();
        showBottomText("力量觉醒！");
        stepDueAt = now + 80;
        stepIdx = 1;
        return;
      }
      if (stepIdx < 1 + 6) {
        // 3 次上下（每次两帧）
        int k = stepIdx - 1;
        bool up = (k % 2) == 0;
        drawWorkoutFrame(up ? 140 : 180);
        drawEyesSmart(0, up ? 30 : 60, true);
        stepDueAt = now + 520;
        stepIdx++;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_CAMERA: {
      if (stepIdx == 0) {
        clearProps();
        drawCameraOnce();
        showBottomText("灵魂出片！");
        drawEyesSmart(0, 60, true);
        stepDueAt = now + 900;
        stepIdx = 1;
        return;
      }
      if (stepIdx < 1 + 6) {
        // 3 次闪光：白屏 -> 回橙并重画
        bool flashOn = ((stepIdx - 1) % 2) == 0;
        if (flashOn) {
          tft.fillScreen(C_WHITE);
        } else {
          tft.fillScreen(C_ORANGE);
          lastLH = 0;
          lastRH = 0;
          drawEyesSmart(0, 60, true);
          drawCameraOnce();
          showBottomText("灵魂出片！");
        }
        stepDueAt = now + (flashOn ? 90 : 520);
        stepIdx++;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_MAGIC: {
      if (stepIdx == 0) {
        clearProps();
        drawMagicWandOnce();
        drawEyesSmart(0, 60, false);
        stepDueAt = now + 80;
        stepIdx = 1;
        return;
      }
      if (stepIdx < 1 + 16) {
        // 小星星点点（画一下再擦一下，局部）
        int sx = (int)random(40, 200);
        int sy = (int)random(120, 180);
        if (((stepIdx - 1) % 2) == 0) tft.fillCircle(sx, sy, 5, C_WHITE);
        else tft.fillCircle(sx, sy, 5, C_ORANGE);
        stepDueAt = now + 120;
        stepIdx++;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_COFFEE: {
      if (stepIdx == 0) {
        clearProps();
        drawCoffeeOnce();
        showBottomText("冷静结界~");
        drawEyesSmart(0, 40, true);
        stepDueAt = now + 120;
        stepIdx = 1;
        return;
      }
      if (stepIdx < 1 + 12) {
        // 蒸汽（两条斜线闪烁）
        bool on = ((stepIdx - 1) % 2) == 0;
        uint16_t col = on ? C_GRAY : C_ORANGE;
        tft.drawLine(110, 150, 115, 135, col);
        tft.drawLine(130, 150, 125, 135, col);
        stepDueAt = now + 260;
        stepIdx++;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_PAINTING: {
      if (stepIdx == 0) {
        clearProps();
        drawPaletteOnce();
        showBottomText("创作开眼！");
        drawEyesSmart(0, 60, false);
        stepDueAt = now + 1100;
        stepIdx = 1;
        return;
      }
      if (stepIdx == 1) {
        drawEyesSmartEx(0, EYE_CY, 60, 0, EYE_CY, 6, false);
        stepDueAt = now + 800;
        stepIdx = 2;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_FISHING: {
      // 钓鱼（四步）：1 抛竿 2 浮漂晃动 3 咬钩挣扎 4 起鱼
      if (stepIdx == 0) {
        clearProps();
        drawEyesSmart(0, 30, false);
        fishingRodRaised = false;
        old_fishVertical = false;
        fishingDrawStatic();
        old_rod_x1 = fishingRodBaseX();
        old_rod_y1 = fishingRodBaseY();
        old_rod_x2 = fishingRodTipX();
        old_rod_y2 = fishingRodTipY();

        showTopRightStageText("起手抛竿");

        // init state
        fish_fy = 206.0f;
        old_fx = 120;
        old_fy = (int)fish_fy;
        old_bobX = 210;   // 抛竿起点：右上飞入
        old_bobY = 150;
        old_line_x1 = fishingRodTipX(); old_line_y1 = fishingRodTipY();
        old_line_x2 = old_bobX; old_line_y2 = old_bobY;
        fishingBite = false;
        fishingBiteUntil = 0;
        fishingNextBiteAt = now + (unsigned long)random(900, 2000);

        stepDueAt = now + 160;
        stepIdx = 1;
        return;
      }

      const int CAST_FRAMES = 26;
      const int FLOAT_FRAMES = 44;
      const int STRUGGLE_FRAMES = 120;
      const int LIFT_FRAMES = 46;
      // 鱼漂始终浮在水面，不做下沉
      const int APPROACH_FRAMES = 18;    // 鱼先游向鱼漂（鱼找漂）
      const int base = 1;
      const int castStart = base;
      const int floatStart = castStart + CAST_FRAMES;
      const int struggleStart = floatStart + FLOAT_FRAMES;
      const int liftStart = struggleStart + STRUGGLE_FRAMES;
      const int endAt = liftStart + LIFT_FRAMES;

      if (stepIdx >= base && stepIdx < endAt) {
        // 计算目标
        int bobX = old_bobX;
        int bobY = old_bobY;
        bool drawBobber = true;

        int fx = old_fx;
        int fy = old_fy;
        bool drawFish = false;
        bool fishVertical = false;

        auto setStageText = [&](const char* txt) { showTopRightStageText(txt); };

        if (stepIdx == castStart) setStageText("起手抛竿");
        if (stepIdx == floatStart) setStageText("静待上钩");
        if (stepIdx == struggleStart) setStageText("咬钩了！");
        if (stepIdx == liftStart) setStageText("提竿！");

        if (stepIdx >= castStart && stepIdx < floatStart) {
          fishingRodRaised = false;
          // 1) 抛竿：浮漂从右上飞入落水点（120,180）
          int i = stepIdx - castStart;
          float p = (float)i / (float)(CAST_FRAMES - 1);
          // 竿子从“高位”落下到常规位（体现抛竿甩线动作）
          int rodBaseX = fishingRodBaseX();
          int rodTipX = fishingRodTipX();
          int rodBaseY = (int)(155 + (175 - 155) * p);
          int rodTipY = (int)(110 + (140 - 110) * p);

          bobX = (int)(210 + (120 - 210) * p);
          bobY = (int)(150 + (180 - 150) * p - 18.0f * sinf(p * 3.14159f));
          bobY = constrain(bobY, 120, 190);

          // 先清理上一帧竿子区域（竿子端点变化会产生残留）
          fishingRestoreRodBBox(old_rod_x1, old_rod_y1, old_rod_x2, old_rod_y2);
          // 更新竿子当前端点（用于背景恢复补画）
          old_rod_x1 = rodBaseX;
          old_rod_y1 = rodBaseY;
          old_rod_x2 = rodTipX;
          old_rod_y2 = rodTipY;
          // 画竿子当前帧
          fishingDrawRod(rodBaseX, rodBaseY, rodTipX, rodTipY);
        } else if (stepIdx >= floatStart && stepIdx < struggleStart) {
          fishingRodRaised = false;
          // 2) 浮漂晃动
          int i = stepIdx - floatStart;
          float t = (float)i * 0.35f;
          bobX = 120 + (int)(sinf(t) * 2.0f);
          bobY = 180 + (int)(sinf(t * 1.7f) * 2.0f);
        } else if (stepIdx >= struggleStart && stepIdx < liftStart) {
          fishingRodRaised = false;
          // 3) 咬钩后挣扎：鱼在深水博弈，浮漂下沉
          int i = stepIdx - struggleStart;
          float wobT = (float)i * 0.28f;
          int bobXsurf = 120 + (int)(sinf(wobT) * 2.0f);
          int bobYsurf = 180 + (int)(sinf(wobT * 1.7f) * 2.0f);

          if (i < APPROACH_FRAMES) {
            // 鱼先游上来找漂：鱼线仍连到漂，鱼在水下靠近漂附近
            float p = (float)i / (float)(APPROACH_FRAMES - 1);
            bobX = bobXsurf;
            bobY = bobYsurf;
            drawBobber = true;

            fx = (int)(120 + (bobX - 120) * p);
            fy = (int)(220 + (195 - 220) * p + sinf((float)i * 0.6f) * 2.0f);
            fy = constrain(fy, 190, 228);

            drawFish = true;
            fishVertical = false;
          } else {
            // 咬钩：鱼漂仍在水面（更剧烈晃动），鱼开始深水挣扎（鱼线连到鱼）
            if (i == APPROACH_FRAMES) drawEyesSmart(0, 85, true);

            int j = i - APPROACH_FRAMES;
            // 让鱼漂只在水面范围内抖动（不下水）
            float bt = (float)j * 0.55f;
            bobX = 120 + (int)(sinf(bt) * 4.0f);
            bobY = 180 + (int)(sinf(bt * 1.6f) * 2.0f);
            bobY = constrain(bobY, 176, 184);
            drawBobber = true;

            float t2 = (float)j * 0.35f;
            int baseFy = 206 + (int)(sinf(t2 * 1.2f) * 4.0f);
            fy = constrain(baseFy, 194, 228);
            fx = 120 + (int)(sinf(t2) * 16.0f);

            if (j == (STRUGGLE_FRAMES - APPROACH_FRAMES) / 2) drawEyesSmart(0, 40, false);

            drawFish = true;
            fishVertical = false;
          }
        } else {
          // 起鱼：提竿更高
          fishingRodRaised = true;
          // 4) 起鱼：浮漂消失，鱼从水里被拉起到空中
          int i = stepIdx - liftStart;
          float p = (float)i / (float)(LIFT_FRAMES - 1);

          drawBobber = false;

          fx = (int)(120 + (150 - 120) * p + sinf(p * 3.14159f) * 10.0f);
          fy = (int)(206 + (140 - 206) * p);
          fy = constrain(fy, 120, 228);

          drawFish = true;
          fishVertical = true; // 起鱼阶段鱼头朝上
          if (i == 0) drawEyesSmart(0, 85, true);
          if (i == LIFT_FRAMES - 1) drawEyesSmart(0, 30, true);
        }

        // 1) 擦上一帧（鱼 / 浮漂 / 鱼线）
        // 鱼尾/鱼头三角会超出 body，擦除范围要更大，避免残留像素
        fishingRestoreRect(old_fx - old_fish_w / 2 - 10, old_fy - old_fish_h / 2 - 12, old_fish_w + 20, old_fish_h + 24);
        fishingRestoreRect(old_bobX - 10, old_bobY - 10, 20, 20);
        {
          // 线条容易产生 1px 残留，扩大擦除范围
          int lx0 = min(old_line_x1, old_line_x2) - 6;
          int ly0 = min(old_line_y1, old_line_y2) - 6;
          int lx1 = max(old_line_x1, old_line_x2) + 6;
          int ly1 = max(old_line_y1, old_line_y2) + 6;
          fishingRestoreRect(lx0, ly0, lx1 - lx0, ly1 - ly0);
        }
        // 竿子（起鱼阶段竿尖会上移，也需要清理上一帧竿子区域）
        fishingRestoreRodBBox(old_rod_x1, old_rod_y1, old_rod_x2, old_rod_y2);

        // 2) 画本帧（先线，再物体）
        int tipX = fishingRodTipX();
        int tipY = fishingRodTipY();
        // 抛竿阶段鱼竿端点在阶段分支内动态绘制；此处不要用“底部固定竿”覆盖
        if (!(stepIdx >= castStart && stepIdx < floatStart)) {
          old_rod_x1 = fishingRodBaseX();
          old_rod_y1 = fishingRodBaseY();
          old_rod_x2 = tipX;
          old_rod_y2 = tipY;
          fishingDrawRod(old_rod_x1, old_rod_y1, old_rod_x2, old_rod_y2);
        } else {
          // 使用抛竿阶段已绘制的竿尖作为鱼线起点
          tipX = old_rod_x2;
          tipY = old_rod_y2;
        }
        if (drawBobber) {
          tft.drawLine(tipX, tipY, bobX, bobY, C_BLACK);
          fishingDrawBobberXY(bobX, bobY);
        } else {
          tft.drawLine(tipX, tipY, fx, fy, C_BLACK);
        }

        if (drawFish) {
          fishingDrawFishUnified(fx, fy, fishVertical, (float)stepIdx * 0.35f);
        }

        // 记录本帧
        old_fx = fx;
        old_fy = fy;
        old_fishVertical = fishVertical;
        // 与 fishingDrawFishUnified 的尺寸保持一致（横/竖互换）
        old_fish_w = fishVertical ? 16 : 24;
        old_fish_h = fishVertical ? 24 : 16;
        old_bobX = bobX;
        old_bobY = bobY;
        old_line_x1 = tipX;
        old_line_y1 = tipY;
        if (drawBobber) {
          old_line_x2 = bobX;
          old_line_y2 = bobY;
        } else {
          old_line_x2 = fx;
          old_line_y2 = fy;
        }

        stepDueAt = now + 55;
        stepIdx++;
        return;
      }

      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_SLEEPING: {
      // 10 分钟冷却：如果未到时间，直接结束并重新调度
      if (stepIdx == 0) {
        if (lastSleepTime > 0 && (now - lastSleepTime) < SLEEP_COOLDOWN) {
          endAction();
          return;
        }
        drawEyesSmart(0, 40, false);
        stepDueAt = now + 200;
        stepIdx = 1;
        return;
      }
      if (stepIdx == 1) {
        drawEyesSmart(0, 20, false);
        stepDueAt = now + 200;
        stepIdx = 2;
        return;
      }
      if (stepIdx == 2) {
        drawEyesSmart(0, 4, false);
        stepDueAt = now + 150;
        stepIdx = 3;
        return;
      }
      if (stepIdx >= 3 && stepIdx < 3 + 12) {
        // 简化版 Z 动画（6 轮，共 12 步：画/擦）
        int i = stepIdx - 3;
        tft.setTextColor(C_WHITE);
        if (i % 2 == 0) {
          tft.setTextSize(1);
          tft.setCursor(145, 60);
          tft.print("z");
          tft.setTextSize(2);
          tft.setCursor(165, 40);
          tft.print("Z");
        } else {
          // 局部擦除 Z 区域
          tft.fillRect(140, 10, 90, 70, C_ORANGE);
        }
        stepDueAt = now + 350;
        stepIdx++;
        return;
      }
      // 结束：恢复
      drawEyesSmart(0, 60, false);
      // 记录真实时间（仅在最后一次播放完成后）
      lastSleepTime = millis();
      endAction();
      return;
    }
    case ACT_PARTY: {
      if (stepIdx == 0) {
        clearProps();
        drawEyesSmart(0, 60, true);
        // 小帽子
        tft.fillTriangle(120, 30, 100, 60, 140, 60, C_YELLOW);
        showBottomText("开趴开趴！");
        stepDueAt = now + 80;
        stepIdx = 1;
        return;
      }
      if (stepIdx < 1 + 32) {
        partyConfettiStep(stepIdx);
        stepDueAt = now + 80;
        stepIdx++;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_MUSIC: {
      if (stepIdx == 0) {
        clearProps();
        drawHeadphonesFrame(true);
        showBottomText("律动降临");
        stepDueAt = now + 120;
        stepIdx = 1;
        return;
      }
      if (stepIdx < 1 + 10) {
        bool left = ((stepIdx - 1) % 2) == 0;
        drawEyesSmart(left ? -15 : 15, 20, true);
        stepDueAt = now + 360;
        stepIdx++;
        return;
      }
      // 擦掉耳机
      drawHeadphonesFrame(false);
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_LOVE: {
      if (stepIdx == 0) {
        clearProps();
        drawEyesSmart(0, 30, true);
        showBottomText("爱心暴击！");
        stepDueAt = now + 80;
        stepIdx = 1;
        return;
      }
      if (stepIdx < 1 + 10) {
        int i = stepIdx - 1;
        // 两个爱心从眼睛附近冒出来
        int ly = 120 + i * 6;
        drawHeart(70, ly, 6, C_PINK);
        drawHeart(170, ly, 6, C_PINK);
        stepDueAt = now + 120;
        stepIdx++;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_LAUGH: {
      if (stepIdx == 0) {
        clearProps();
        showBottomText("哈哈哈！");
        stepDueAt = now + 80;
        stepIdx = 1;
        return;
      }
      if (stepIdx < 1 + 10) {
        int i = stepIdx - 1;
        int h = (i % 2 == 0) ? 18 : 26; // 眯眼笑
        drawEyesSmart(0, h, true);
        // 小小“笑泪”
        if (i % 2 == 0) {
          tft.fillCircle(eyeLX(0) + 8, EYE_CY + 38, 4, C_CYAN);
          tft.fillCircle(eyeRX(0) + 8, EYE_CY + 38, 4, C_CYAN);
        } else {
          tft.fillCircle(eyeLX(0) + 8, EYE_CY + 38, 4, C_ORANGE);
          tft.fillCircle(eyeRX(0) + 8, EYE_CY + 38, 4, C_ORANGE);
        }
        stepDueAt = now + 160;
        stepIdx++;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_ANGRY: {
      if (stepIdx == 0) {
        clearProps();
        showBottomText("我生气了！");
        // 眉毛（两条斜线）
        tft.drawLine(eyeLX(0), EYE_CY - 38, eyeLX(0) + 26, EYE_CY - 26, C_BLACK);
        tft.drawLine(eyeRX(0) + 26, EYE_CY - 38, eyeRX(0), EYE_CY - 26, C_BLACK);
        stepDueAt = now + 120;
        stepIdx = 1;
        return;
      }
      if (stepIdx < 1 + 8) {
        int i = stepIdx - 1;
        int ox = (i % 2 == 0) ? -6 : 6;
        drawEyesSmart(ox, 28, false);
        stepDueAt = now + 120;
        stepIdx++;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_CRY: {
      if (stepIdx == 0) {
        clearProps();
        showBottomText("呜呜呜…");
        drawEyesSmart(0, 20, false);
        stepDueAt = now + 100;
        stepIdx = 1;
        return;
      }
      if (stepIdx < 1 + 14) {
        int i = stepIdx - 1;
        // 两条泪滴往下掉（局部擦除）
        int y = 120 + i * 6;
        tft.fillRect(eyeLX(0) + 10, y, 6, 16, C_CYAN);
        tft.fillRect(eyeRX(0) + 10, y, 6, 16, C_CYAN);
        stepDueAt = now + 120;
        stepIdx++;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_SPARKLE: {
      if (stepIdx == 0) {
        clearProps();
        showBottomText("眼里有光！");
        drawEyesSmart(0, 60, false);
        stepDueAt = now + 80;
        stepIdx = 1;
        return;
      }
      if (stepIdx < 1 + 10) {
        int i = stepIdx - 1;
        // 在眼睛周围闪闪发光
        uint16_t col = (i % 2 == 0) ? C_WHITE : C_YELLOW;
        drawSparkle(40, 40, 8, col);
        drawSparkle(200, 50, 6, col);
        drawSparkle(30, 160, 6, col);
        drawSparkle(210, 170, 8, col);
        stepDueAt = now + 140;
        stepIdx++;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_WAVE: {
      if (stepIdx == 0) {
        clearProps();
        showBottomText("嗨嗨~");
        drawEyesSmart(0, 40, true);
        stepDueAt = now + 80;
        stepIdx = 1;
        return;
      }
      if (stepIdx < 1 + 10) {
        bool up = ((stepIdx - 1) % 2) == 0;
        // 擦一下右侧手区域再画
        tft.fillRect(185, 130, 55, 80, C_ORANGE);
        drawWaveHandFrame(198, 155, up, C_PINK);
        stepDueAt = now + 140;
        stepIdx++;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_THUMBS_UP: {
      if (stepIdx == 0) {
        clearProps();
        showBottomText("给你点赞！");
        drawEyesSmart(0, 40, true);
        stepDueAt = now + 80;
        stepIdx = 1;
        return;
      }
      if (stepIdx < 1 + 8) {
        int i = stepIdx - 1;
        // 轻微“弹一下”效果
        int dy = (i % 2 == 0) ? 0 : -3;
        tft.fillRect(140, 130, 100, 90, C_ORANGE);
        drawThumbOnce(160, 160 + dy, C_YELLOW);
        stepDueAt = now + 160;
        stepIdx++;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_SURPRISED: {
      if (stepIdx == 0) {
        clearProps();
        showBottomText("震惊到失语！");
        stepDueAt = now + 60;
        stepIdx = 1;
        return;
      }
      if (stepIdx < 1 + 8) {
        int i = stepIdx - 1;
        int h = 80 - (i % 2) * 8;
        drawEyesSmart(0, h, (i % 2) == 0);
        stepDueAt = now + 140;
        stepIdx++;
        return;
      }
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_SLEEP_MASK: {
      if (stepIdx == 0) {
        clearProps();
        showBottomText("进入省电模式…");
        drawEyesSmart(0, 4, false);
        // 眼罩
        tft.fillRoundRect(40, 60, 160, 50, 18, C_BLACK);
        tft.fillRect(0, 82, 40, 6, C_BLACK);
        tft.fillRect(200, 82, 40, 6, C_BLACK);
        stepDueAt = now + 1400;
        stepIdx = 1;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    case ACT_HEART_RAIN: {
      if (stepIdx == 0) {
        clearProps();
        showBottomText("爱心雨降临！");
        drawEyesSmart(0, 30, true);
        stepDueAt = now + 80;
        stepIdx = 1;
        return;
      }
      if (stepIdx < 1 + 24) {
        // 随机小爱心落下（画一次即可，背景由下一次 clearProps 统一清）
        int x = 20 + (int)random(0, 200);
        int y = 40 + (int)random(0, 150);
        drawHeart(x, y, 4, C_PINK);
        stepDueAt = now + 80;
        stepIdx++;
        return;
      }
      clearProps();
      drawEyesSmart(0, 60, false);
      endAction();
      return;
    }
    default:
      endAction();
      return;
  }
}

void expressionModeEnter() {
  initColoursOnce();
  clearProps();

  lastLX = 0;
  lastRX = 0;
  lastLY = EYE_CY;
  lastRY = EYE_CY;
  lastLH = 60;
  lastRH = 60;
  lastBlush = false;

  busy = false;
  currentAction = -1;
  stepIdx = 0;
  stepDueAt = 0;

  drawEyesSmart(0, 60, false);
  nextActionTime = millis() + 800;
}

void expressionModeTick() {
  initColoursOnce();
  const unsigned long nowReal = millis();
  const unsigned long now = nowReal;

  if (!busy) {
    if (now < nextActionTime) return;

    // 恢复全部表情随机，并移除“钓鱼”表情
    int a = ACT_BLINK;
    for (int tries = 0; tries < 12; tries++) {
      a = random(0, ACT_COUNT);
      // 移除：魔法/泡泡/音乐/钓鱼
      if (a == ACT_MAGIC || a == ACT_BUBBLES || a == ACT_MUSIC || a == ACT_FISHING) continue;
      break;
    }
    if (a == ACT_MAGIC || a == ACT_BUBBLES || a == ACT_MUSIC || a == ACT_FISHING) a = ACT_BLINK;

    // 睡觉表情限流（10 分钟冷却）
    if (a == ACT_SLEEPING && lastSleepTime > 0 && (nowReal - lastSleepTime) < SLEEP_COOLDOWN) {
      a = ACT_BLINK;
    }

    startAction(a);
  }

  actionStep(now);
}

