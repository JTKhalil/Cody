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
  ACT_BLINK = 0,
  ACT_HAPPY = 1,
  ACT_LOOK_AROUND = 2,
  ACT_WINK_LEFT = 3,
  ACT_WINK_RIGHT = 4,
  ACT_DIZZY = 5,
  ACT_GAMING = 6,
  ACT_FISHING = 7,
  ACT_SLEEPING = 8,
  ACT_COUNT = 9
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

static void fishingDrawStatic() {
  // 静态：竿/水面/浮标
  tft.drawLine(40, 200, 120, 140, C_BROWN);
  tft.drawLine(120, 140, 120, 180, C_GRAY);
  tft.fillRect(0, 180, 240, 60, C_BLUE);
  tft.fillCircle(120, 180, 6, C_RED);
}

static int old_fx = 120;
static int old_fy = 205;
static float fish_fy = 205.0f;

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
    case ACT_GAMING: {
      if (stepIdx == 0) {
        drawGamingPropsOnce();
        tft.setTextColor(C_WHITE);
        tft.setTextSize(2);
        tft.setCursor(75, 215);
        tft.print("Gaming!");
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
    case ACT_FISHING: {
      // 深水挣扎简化版（无大面积闪屏）：只做少量帧
      if (stepIdx == 0) {
        clearProps();
        drawEyesSmart(0, 30, false);
        fishingDrawStatic();
        stepDueAt = now + 600;
        stepIdx = 1;
        old_fx = 120;
        old_fy = (int)fish_fy;
        return;
      }
      if (stepIdx == 1) {
        // 猛沉 + 震惊
        tft.fillCircle(120, 180, 6, C_BLUE);
        tft.fillCircle(120, 190, 6, C_RED);
        drawEyesSmart(0, 85, true);
        tft.setTextColor(C_WHITE);
        tft.setTextSize(2);
        tft.setCursor(150, 120);
        tft.print("Got it!");
        stepDueAt = now + 250;
        stepIdx = 2;
        return;
      }
      if (stepIdx >= 2 && stepIdx < 2 + 24) {
        // 擦除上一帧鱼附近区域
        tft.fillRect(old_fx - 26, old_fy - 14, 52, 28, C_BLUE);
        // 重绘水面和竿（局部）
        tft.fillRect(0, 180, 240, 60, C_BLUE);
        tft.drawLine(40, 200, 120, 140, C_BROWN);

        float t = (float)(stepIdx - 2) * 0.5f;
        int fx = 120 + (int)(sinf(t) * 18.0f);
        int fy = (int)fish_fy;

        tft.drawLine(120, 140, fx, fy, C_BLACK);
        tft.fillRoundRect(fx - 12, fy - 8, 24, 16, 4, C_CYAN);
        if (cosf(t) > 0) {
          tft.fillTriangle(fx - 12, fy, fx - 24, fy - 12, fx - 24, fy + 12, C_CYAN);
          tft.fillCircle(fx + 6, fy - 2, 3, C_BLACK);
          tft.fillCircle(fx + 7, fy - 3, 1, C_WHITE);
        } else {
          tft.fillTriangle(fx + 12, fy, fx + 24, fy - 12, fx + 24, fy + 12, C_CYAN);
          tft.fillCircle(fx - 6, fy - 2, 3, C_BLACK);
          tft.fillCircle(fx - 7, fy - 3, 1, C_WHITE);
        }

        old_fx = fx;
        old_fy = fy;
        stepDueAt = now + 50;
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
      lastSleepTime = now;
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
  unsigned long now = millis();

  if (!busy) {
    if (now < nextActionTime) return;

    int a = random(0, ACT_COUNT);
    // 轻微倾向更常用的表情（避免太“重”的道具动作频率过高）
    if (a == ACT_FISHING && random(0, 100) < 70) a = ACT_BLINK;
    if (a == ACT_SLEEPING && random(0, 100) < 60) a = ACT_BLINK;

    startAction(a);
  }

  actionStep(now);
}

