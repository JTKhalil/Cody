#include "include/globals.h"
#include "include/render/expression_mode.h"
#include "include/render/expression_pose.h"
#include "include/render/expression_renderer.h"

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

static ExpressionRenderer g_exprRenderer;
static bool g_exprInited = false;

enum ExprGroupId : uint8_t {
  EXPR_GROUP_BUBU = 0, // 新：渲染器差分表情（眨眼 + 左右看）
  EXPR_GROUP_COCO = 1, // 旧：actionStep() 那套多动作表情
};
static ExprGroupId g_exprGroup = EXPR_GROUP_BUBU;

// --- Expressions ---
// Expression #1: blink both eyes (base -> blink -> base)
// Expression #2: pupils look right -> left -> center (base open, only gazeX changes)
// Expression #3: pupils circle in one direction (base open, gazeX+gazeY change)
// Expression #4: half-close both eyes, hold 3s, then open
// Expression #5: cry — 双眼紧闭开合 + 眼下蓝色竖条泪 + 伤心嘴
// Expression #6: pupils look down -> up -> center（时间轴同左右看，gazeY）
// Expression #7: sing — 全睁、O 嘴、斜向话筒 + 底部歌词
// Expression #8: sleep — 闭眼白线 + animated ZZZ（循环最后一项）
// Expression #9: pupils circle，与 #3 同轨迹仅旋转方向相反
// Expression #10: pupils look up -> down -> center（与 #6 同节奏，gazeY 取反）
static int g_exprIdx = 0;                 // 0..9
static bool g_exprPlaying = false;
static unsigned long g_exprStartAt = 0;
static unsigned long g_nextExprAt = 0;
// 卜卜睡觉（g_exprIdx==7）播完后记录，10 分钟内不再随机到睡觉
static unsigned long g_bubuLastSleepEndedAt = 0;
// 卜卜唱歌 UI（话筒+歌词）是否处于显示中；离开唱歌段时兜底清屏，避免切换表情后残字
static bool g_bubuSingLyricLatch = false;

static constexpr unsigned long kExprGapMs = 3000;   // after returning to base
static constexpr unsigned long kBubuSleepCooldownMs = 600000UL; // 10 分钟
static constexpr unsigned long kBlinkDurMs = 220;   // fast + sample-friendly
static constexpr unsigned long kLookLRDurMs = 2000; // right -> left -> center (longer holds)
static constexpr unsigned long kLookUDDurMs = 2000; // down -> up -> center（与左右看同节奏）
static constexpr unsigned long kSingDurMs = 8000; // 唱歌时长 8s
static constexpr float kSingMouthOMin = 0.38f;
static constexpr float kSingMouthOMax = 0.64f;
static constexpr float kSingMouthOCycleMs = 980.f; // O 型嘴大小起伏周期
static constexpr unsigned long kCircleDurMs = 1600; // pupil circle (idx 2 / 8, same duration)
static constexpr unsigned long kHalfCloseInMs = 220;
static constexpr unsigned long kHalfCloseHoldMs = 3000;
static constexpr unsigned long kHalfCloseOutMs = 260;

static constexpr unsigned long kSleepCloseMs = 320;
static constexpr unsigned long kSleepHoldMs = 60000;
// 睡醒时先清 ZZZ 再抬眼皮，避免擦除区盖住不完整眼白
static constexpr unsigned long kSleepZzzPreOpenMs = 72;
static constexpr unsigned long kSleepOpenMs = 340;

// prevIdx < 0：进入表情模式时首抽，不检查“与上一段相同”
static int pickNextBubuExprIdx(int prevIdx, unsigned long now) {
  const bool sleepAllowed =
      (g_bubuLastSleepEndedAt == 0 || (now - g_bubuLastSleepEndedAt) >= kBubuSleepCooldownMs);
  for (int attempt = 0; attempt < 48; attempt++) {
    const int n = (int)random(0, 10);
    if (prevIdx >= 0 && n == prevIdx) continue;
    if (n == 7 && !sleepAllowed) continue;
    return n;
  }
  for (int n = 0; n < 10; n++) {
    if (prevIdx >= 0 && n == prevIdx) continue;
    if (n == 7 && !sleepAllowed) continue;
    return n;
  }
  if (prevIdx >= 0) return (prevIdx + 1) % 10;
  return 0;
}
// eyeRy must stay > ~1 so renderer stays in “slit” mode, not full closed line
// 须使 eyeRy<=1 才会走 drawClosedEyeLine；0.019→ry=2 仍是细缝椭圆，左右会尖细。0 为等宽闭眼白线。
static constexpr float kSleepSlitOpen = 0.0f;
// 小于此开合度再隐藏瞳孔（接近缝眼时才消失，闭眼过程中仍可见眼珠）
static constexpr float kSleepHidePupilOpenMax = 0.055f;
static constexpr float kSleepMouthOCycleMs = 3800.f;

// 哭泣：闭眼入 → 眼泪柱状流下 kCryTearFlowMs → 泪柱静止 kCryTearHoldMs（5s）→ 睁眼出
static constexpr unsigned long kCryInMs = 220;
static constexpr unsigned long kCryTearFlowMs = 750;
static constexpr unsigned long kCryTearHoldMs = 5000;
static constexpr unsigned long kCryHoldMs = kCryTearFlowMs + kCryTearHoldMs;
static constexpr unsigned long kCryOutMs = 380; // 略长 + 渲染端补缝，减轻睁眼过程橙底闪动
static constexpr int16_t kCryPillarW = 12;
// 擦除条略宽于泪柱，避免蓝边残留；仅擦两眼下方竖带，不扫过嘴区（避免嘴闪、泪迹被嘴盖住）
static constexpr int16_t kCryClearW = 17;

// 卜卜唱歌（g_exprIdx==6）：底部随机一句，单行截断；嘴整体上移，歌词带自 kExprLyricBandTopY 起与嘴清屏不重叠
static const char* const kBubuSingLyrics[] = {
    "逆风的方向，更适合飞翔",
    "笑着哭，最痛",
    "最怕突然听到你的消息",
    "你不是真正的快乐",
    "你的笑只是你穿的保护色",
    "坚持对我来说，就是以刚克刚",
    "咸鱼也要有梦",
    "最单纯的笑脸和最美那一年",
    "生命有一种绝对",
    "我们像一首最美丽的歌曲",
};
static constexpr int kBubuSingLyricsCount = (int)(sizeof(kBubuSingLyrics) / sizeof(kBubuSingLyrics[0]));

static inline float clamp01_local(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
static inline float smoothstep_local(float t) { t = clamp01_local(t); return t * t * (3.0f - 2.0f * t); }
static inline float lerp_local(float a, float b, float t) { return a + (b - a) * t; }
static inline float clamp_local(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float quant_local(float x, float step) {
  if (step <= 0.0f) return x;
  return step * lroundf(x / step);
}
static float blink_profile_local(float t01) {
  const float t = clamp01_local(t01);
  if (t < 0.35f) return smoothstep_local(t / 0.35f);           // 0 -> 1
  if (t < 0.55f) return 1.0f;                                  // hold
  return 1.0f - smoothstep_local((t - 0.55f) / 0.45f);         // 1 -> 0
}

static float look_lr_profile_local(float t01, float amp) {
  // Longer holds on right/left; quicker moves between (still smooth).
  // 0..0.15 move to +amp, 0.15..0.42 hold right
  // 0.42..0.57 move to -amp, 0.57..0.84 hold left
  // 0.84..1.00 return to 0
  const float t = clamp01_local(t01);
  const float a = clamp_local(amp, 0.0f, 1.0f);
  if (t < 0.15f) return lerp_local(0.0f, +a, smoothstep_local(t / 0.15f));
  if (t < 0.42f) return +a;
  if (t < 0.57f) return lerp_local(+a, -a, smoothstep_local((t - 0.42f) / 0.15f));
  if (t < 0.84f) return -a;
  return lerp_local(-a, 0.0f, smoothstep_local((t - 0.84f) / 0.16f));
}

static ExprFrame expr_base_frame_static() {
  ExprFrame f{};
  f.gazeX = 0.0f;
  f.gazeY = 0.0f;
  f.openL = 0.62f;
  f.openR = 0.62f;
  f.pupil = 0.55f;
  f.crossEyeT = 1.0f;
  f.hidePupil = false;
  f.sleepMouthO = 0.0f;
  f.mouthCxOfs = 0;
  f.mouth = MouthId::WAVY;
  f.brow = BrowId::NEUTRAL;
  return f;
}

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
  ACT_FLOWER = 17,
  ACT_READING = 18,
  ACT_EATING = 19,
  ACT_GAMING = 20,
  ACT_WORKOUT = 21,
  ACT_CAMERA = 22,
  ACT_MAGIC = 23,
  ACT_COFFEE = 24,
  ACT_PAINTING = 25,
  ACT_PARTY = 26,
  ACT_MUSIC = 27,
  ACT_LAUGH = 28,
  ACT_ANGRY = 29,
  ACT_CRY = 30,
  ACT_SPARKLE = 31,
  ACT_WAVE = 32,
  ACT_THUMBS_UP = 33,
  ACT_SURPRISED = 34,
  ACT_COUNT = 35
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

static int utf8FirstCharLen(uint8_t c) {
  if ((c & 0x80) == 0) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

static void utf8PopLastChar(String& s) {
  if (s.length() == 0) return;
  int i = (int)s.length() - 1;
  while (i > 0 && ((uint8_t)s.charAt(i) & 0xC0) == 0x80) i--;
  s = s.substring(0, (unsigned)i);
}

// 歌词带：wqy16 单行显示；上扩 3px 清抗锯齿残影；基线距屏底留白，避免贴边
static constexpr int16_t kSingLyricBandY = kExprLyricBandTopY;
static constexpr int16_t kSingLyricBottomTextPad = 10;
static constexpr int16_t kSingLyricBandH = (int16_t)(DISP_H - kSingLyricBandY);
static constexpr int16_t kSingLyricClearPadTop = 3;

static inline void fillSingLyricBandBg() {
  initColoursOnce();
  const int16_t y = (int16_t)(kSingLyricBandY - kSingLyricClearPadTop);
  const int16_t h = (int16_t)(kSingLyricBandH + kSingLyricClearPadTop);
  tft.fillRect(0, y, DISP_W, h, C_ORANGE);
}

/** 唱歌专用：进入本段唱歌时调用一次即可（嘴与话筒不再写入该带，无需每帧重绘，避免闪屏） */
static void showSingLyricsWrapped(const char* utf8) {
  fillSingLyricBandBg();
  if (!utf8 || !utf8[0]) return;

  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setForegroundColor(C_WHITE);
  u8g2.setBackgroundColor(C_ORANGE);
  u8g2.setFontMode(0);

  const int fa = u8g2.getFontAscent();
  const int fd = u8g2.getFontDescent();
  // 基线在带内上移，与屏底留 kSingLyricBottomTextPad（另 2px 防裁切）
  const int baseline =
      (int)(kSingLyricBandY + kSingLyricBandH - kSingLyricBottomTextPad - 2);

  const int maxW = 220;
  const int x0 = 10;
  constexpr const char* kEll = "…";

  String line(utf8);
  line.trim();
  if (line.length() == 0) {
    u8g2.setFontMode(1);
    return;
  }

  if (u8g2.getUTF8Width(line.c_str()) > maxW) {
    String core = line;
    line = core + kEll;
    while (core.length() > 0 && u8g2.getUTF8Width(line.c_str()) > maxW) {
      utf8PopLastChar(core);
      line = core + kEll;
    }
  }

  u8g2.setCursor(x0, baseline);
  u8g2.print(line);

  u8g2.setFontMode(1);
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

// 与 expression_renderer.cpp drawMouth 一致（嘴仍用此常量）
static constexpr int16_t kExprMouthCx = 120;
static constexpr int16_t kExprMouthY = 166;

// ZZZ 锚在嘴的右上角外（再上、再右，避开 O 嘴动画）
static void sleepZzzGlyphPositions(int16_t bx[3], int16_t by[3]) {
  const int16_t ax = (int16_t)(kExprMouthCx + 38);
  const int16_t ay = (int16_t)(kExprMouthY - 32);
  bx[0] = (int16_t)(ax - 12);
  bx[1] = (int16_t)(ax + 2);
  bx[2] = (int16_t)(ax + 14);
  by[0] = (int16_t)(ay + 4);
  by[1] = (int16_t)(ay - 6);
  by[2] = (int16_t)(ay - 16);
}

// 三个 Z（字号 1,1,2）完整占位的外包矩形；动画每帧用同一矩形清底，避免 union 上一帧导致区域忽大忽小闪屏
static void sleepZzzFixedPaintBounds(int16_t& outX, int16_t& outY, int16_t& outW, int16_t& outH) {
  static constexpr uint16_t kLayoutTag = 5u;
  static uint16_t s_layoutTag = 0;
  static int16_t s_x, s_y, s_w, s_h;
  if (s_layoutTag == kLayoutTag) {
    outX = s_x;
    outY = s_y;
    outW = s_w;
    outH = s_h;
    return;
  }
  initColoursOnce();
  int16_t bx[3], by[3];
  sleepZzzGlyphPositions(bx, by);
  const uint8_t szs[3] = {1, 1, 2};
  int32_t minX = 1000, minY = 1000, maxX = 0, maxY = 0;
  for (int i = 0; i < 3; i++) {
    tft.setTextSize(szs[i]);
    int16_t gb, gby;
    uint16_t gw, gh;
    tft.getTextBounds("Z", bx[i], by[i], &gb, &gby, &gw, &gh);
    minX = min(minX, (int32_t)gb);
    minY = min(minY, (int32_t)gby);
    maxX = max(maxX, (int32_t)gb + (int32_t)gw);
    maxY = max(maxY, (int32_t)gby + (int32_t)gh);
  }
  tft.setTextSize(1);
  constexpr int16_t kPad = 4;
  s_x = (int16_t)(minX - kPad);
  s_y = (int16_t)(minY - kPad);
  s_w = (int16_t)(maxX - minX + 2 * kPad);
  s_h = (int16_t)(maxY - minY + 2 * kPad);
  s_layoutTag = kLayoutTag;
  outX = s_x;
  outY = s_y;
  outW = s_w;
  outH = s_h;
}

static void bubuSleepZzzForeheadClear() {
  initColoursOnce();
  int16_t x, y, w, h;
  sleepZzzFixedPaintBounds(x, y, w, h);
  tft.fillRect(x, y, w, h, C_ORANGE);
}

// bubu #5：ZZZ 在嘴右上角外，偏小，斜向；逐个出现再逐个消失
static void drawBubuSleepZzz(unsigned long tLocalMs) {
  initColoursOnce();
  int16_t bx[3], by[3];
  sleepZzzGlyphPositions(bx, by);
  const uint8_t szs[3] = {1, 1, 2};
  int16_t px, py, pw, ph;
  sleepZzzFixedPaintBounds(px, py, pw, ph);

  constexpr unsigned long kStep = 320;
  constexpr unsigned long kCycle = kStep * 6;
  const unsigned long c = tLocalMs % kCycle;
  uint8_t vis = 0;
  if (c < kStep)
    vis = 1;
  else if (c < 2 * kStep)
    vis = 3;
  else if (c < 3 * kStep)
    vis = 7;
  else if (c < 4 * kStep)
    vis = 6;
  else if (c < 5 * kStep)
    vis = 4;
  else
    vis = 0;

  tft.fillRect(px, py, pw, ph, C_ORANGE);

  tft.setTextWrap(false);
  tft.setTextColor(C_WHITE, C_ORANGE);
  for (int i = 0; i < 3; i++) {
    if (((vis >> i) & 1) == 0) continue;
    tft.setTextSize(szs[i]);
    tft.setCursor(bx[i], by[i]);
    tft.print('Z');
  }
  tft.setTextSize(1);
}

// 哭泣：只擦两眼泪柱竖带（橙底），不铺整屏宽条，避免盖住嘴与切断泪迹
static void bubuCryTearsClearPillarStrips(int16_t yTop, int16_t yBot, int16_t cxL, int16_t cxR, int16_t clearW) {
  initColoursOnce();
  if (yBot <= yTop || clearW < 2) return;
  const int16_t h = (int16_t)(yBot - yTop);
  auto strip = [&](int16_t cx) {
    int16_t x = (int16_t)(cx - clearW / 2);
    if (x < 0) x = 0;
    if (x + clearW > DISP_W) x = (int16_t)(DISP_W - clearW);
    tft.fillRect(x, yTop, clearW, h, C_ORANGE);
  };
  strip(cxL);
  strip(cxR);
}

// 两眼下方柱状泪：仅在流下开始前清一次底；流下过程只向下加长竖条；静止 5s 不重绘以压闪
static void drawBubuCryTears(const ExprFrame& f, unsigned long elLocal) {
  initColoursOnce();
  static unsigned long s_prevElCry = 0xffffffffUL;
  static bool s_flowBandCleared = false;
  static bool s_holdPainted = false;

  const EyeGeom L = expression_eye_left();
  const EyeGeom R = expression_eye_right();
  const int16_t ryL = expression_eye_ry_from_open(f.openL);
  const int16_t ryR = expression_eye_ry_from_open(f.openR);
  const int16_t yBaseL = (int16_t)(L.cy + max((int16_t)3, ryL) + 2);
  const int16_t yBaseR = (int16_t)(R.cy + max((int16_t)3, ryR) + 2);
  const int16_t yTop = (int16_t)min((int)yBaseL, (int)yBaseR);
  const int16_t yBot = DISP_H; // fillRect 高度用 yBot - yTop，泪柱画到屏底

  if (elLocal < kCryInMs) {
    s_flowBandCleared = false;
    s_holdPainted = false;
    s_prevElCry = elLocal;
    return;
  }

  if (elLocal >= kCryInMs + kCryHoldMs) {
    const bool enteredOut = (s_prevElCry < kCryInMs + kCryHoldMs);
    s_prevElCry = elLocal;
    if (enteredOut) bubuCryTearsClearPillarStrips((int16_t)(yTop - 2), yBot, L.cx, R.cx, kCryClearW);
    return;
  }

  const unsigned long tRel = elLocal - kCryInMs;
  const uint16_t tear = tft.color565(28, 105, 255);
  const int16_t maxHL = (int16_t)(DISP_H - yBaseL);
  const int16_t maxHR = (int16_t)(DISP_H - yBaseR);

  auto drawOnePillar = [&](int16_t cx, int16_t yBase, int16_t maxH, int16_t pillarH) {
    const int16_t h = (int16_t)min((int)maxH, (int)pillarH);
    if (h <= 0) return;
    const int16_t x = (int16_t)(cx - kCryPillarW / 2);
    tft.fillRect(x, yBase, kCryPillarW, h, tear);
  };

  if (tRel < kCryTearFlowMs) {
    if (!s_flowBandCleared) {
      bubuCryTearsClearPillarStrips((int16_t)(yTop - 2), yBot, L.cx, R.cx, kCryClearW);
      s_flowBandCleared = true;
      s_holdPainted = false;
    }
    const float u = smoothstep_local((float)tRel / (float)max(1UL, kCryTearFlowMs));
    const int16_t hL = (int16_t)min((int)maxHL, (int)lroundf(4.0f + u * (float)max(1, (int)maxHL - 4)));
    const int16_t hR = (int16_t)min((int)maxHR, (int)lroundf(4.0f + u * (float)max(1, (int)maxHR - 4)));
    drawOnePillar(L.cx, yBaseL, maxHL, hL);
    drawOnePillar(R.cx, yBaseR, maxHR, hR);
  } else {
    if (!s_holdPainted) {
      drawOnePillar(L.cx, yBaseL, maxHL, maxHL);
      drawOnePillar(R.cx, yBaseR, maxHR, maxHR);
      s_holdPainted = true;
    }
  }
  s_prevElCry = elLocal;
}

static void drawMicOnce() {
  // 简化麦克风（右侧）
  tft.fillRoundRect(160, 160, 16, 40, 8, C_GRAY);
  tft.fillCircle(168, 150, 15, C_BLACK);
}

// 唱歌：🎤 风格 — 大圆网罩 + 银环 + 粉红握身段 + 深色尾把（沿斜轴绘制）
static void drawSingMicOnce() {
  initColoursOnce();
  const uint16_t meshCore = tft.color565(20, 20, 26);
  const uint16_t meshShade = tft.color565(38, 38, 48);
  const uint16_t meshHi = tft.color565(72, 72, 88);
  const uint16_t colGrille = tft.color565(52, 52, 64);
  const uint16_t colSilver = tft.color565(218, 218, 228);
  const uint16_t colSilverDim = tft.color565(140, 138, 152);
  const uint16_t colBody = tft.color565(238, 72, 108);
  const uint16_t colBodyHi = tft.color565(255, 140, 165);
  const uint16_t colGrip = tft.color565(48, 46, 54);

  const int16_t hx = 172;
  const int16_t hy = 176;
  const int16_t gx = 228;
  // 握把末端低于歌词带 kSingLyricBandY，避免每帧话筒画进歌词区后又被橙带盖住造成闪烁
  const int16_t gy = 192;

  float fx = (float)(gx - hx);
  float fy = (float)(gy - hy);
  const float L = sqrtf(fx * fx + fy * fy);
  if (L < 1.0f) return;
  fx /= L;
  fy /= L;
  const float pxn = -fy;
  const float pyn = fx;

  const float headR = 14.5f;
  const float dMin = headR - 0.5f;
  const float dMax = L;
  if (dMax <= dMin) return;

  for (float d = dMax; d >= dMin; d -= 2.05f) {
    const float u = (d - dMin) / (dMax - dMin);
    int16_t cx = (int16_t)lroundf(hx + fx * d);
    int16_t cy = (int16_t)lroundf(hy + fy * d);
    uint16_t col;
    int16_t rad;
    if (u < 0.10f) {
      col = colSilver;
      rad = 5;
    } else if (u < 0.14f) {
      col = colSilverDim;
      rad = 4;
    } else if (u < 0.45f) {
      col = (u < 0.28f) ? colBodyHi : colBody;
      rad = (int16_t)lroundf(6.2f - 0.8f * (u - 0.14f));
      if (u > 0.22f && u < 0.38f) {
        int16_t hx2 = (int16_t)lroundf(cx + pxn * 2.2f);
        int16_t hy2 = (int16_t)lroundf(cy + pyn * 2.2f);
        tft.fillCircle(hx2, hy2, 2, colBodyHi);
      }
    } else {
      col = colGrip;
      rad = (int16_t)lroundf(3.8f + 1.2f * u);
    }
    tft.fillCircle(cx, cy, (int16_t)max((int16_t)2, rad), col);
  }

  tft.fillCircle(hx, hy, 15, meshShade);
  tft.fillCircle(hx, hy, 14, meshCore);
  tft.drawCircle(hx, hy, 15, colSilverDim);
  tft.drawCircle(hx, hy, 14, colGrille);

  for (int dy = -13; dy <= 11; dy += 2) {
    const float rr = headR * headR - (float)(dy * dy);
    if (rr < 0.0f) continue;
    const int aw = (int)lroundf(sqrtf(rr));
    if (aw <= 0) continue;
    const uint16_t gc = (dy < -5) ? colGrille : meshHi;
    tft.drawFastHLine((int16_t)(hx - aw), (int16_t)(hy + dy), (int16_t)(2 * aw + 1), gc);
  }
  for (int dx = -12; dx <= 12; dx += 4) {
    const float rr = headR * headR - (float)(dx * dx);
    if (rr < 0.0f) continue;
    const int ah = (int)lroundf(sqrtf(rr));
    if (ah <= 0) continue;
    tft.drawFastVLine((int16_t)(hx + dx), (int16_t)(hy - ah), (int16_t)(2 * ah + 1), colGrille);
  }

  tft.fillCircle((int16_t)lroundf(hx + pxn * 4.f - 2.f), (int16_t)lroundf(hy + pyn * 4.f - 3.f), 5, meshHi);
  tft.drawCircle((int16_t)lroundf(hx + pxn * 4.f - 2.f), (int16_t)lroundf(hy + pyn * 4.f - 3.f), 5, colSilverDim);
  tft.fillCircle((int16_t)lroundf(hx + pxn * 2.5f), (int16_t)lroundf(hy + pyn * 2.5f), 2, C_WHITE);

  const int16_t bx = (int16_t)lroundf(hx + fx * (headR + 1.2f));
  const int16_t by = (int16_t)lroundf(hy + fy * (headR + 1.2f));
  for (int k = -3; k <= 3; k++) {
    int16_t ox = (int16_t)lroundf(bx + pxn * (float)k * 1.8f);
    int16_t oy = (int16_t)lroundf(by + pyn * (float)k * 1.8f);
    tft.drawPixel(ox, oy, colSilver);
    if (abs(k) <= 2) tft.drawPixel((int16_t)(ox + lroundf(fx)), (int16_t)(oy + lroundf(fy)), colSilverDim);
  }
}

static void clearSingOverlay() {
  initColoursOnce();
  tft.fillRect(124, 140, 126, 98, C_ORANGE);
  fillSingLyricBandBg();
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
    default:
      endAction();
      return;
  }
}

void expressionModeEnter() {
  g_exprGroup = (exprGroup == 1) ? EXPR_GROUP_COCO : EXPR_GROUP_BUBU;

  const unsigned long now = millis();
  if (g_exprGroup == EXPR_GROUP_BUBU) {
    // bubu：基准表情作为起点；眨眼后回到基准表情
    if (!g_exprInited) g_exprInited = true;
    g_bubuSingLyricLatch = false;
    g_exprRenderer.enter();
    g_exprRenderer.tick(expr_base_frame_static());
    g_exprIdx = pickNextBubuExprIdx(-1, now);
    g_exprPlaying = false;
    g_exprStartAt = 0;
    g_nextExprAt = now + kExprGapMs;
  } else {
    // coco：沿用 actionStep() 表情组
    initColoursOnce();
    clearProps();
    // 重置差分眼睛缓存，避免从别的模式切入时残影
    lastLX = 0; lastRX = 0;
    lastLY = EYE_CY; lastRY = EYE_CY;
    lastLH = 60; lastRH = 60;
    lastBlush = false;
    busy = false;
    currentAction = -1;
    stepIdx = 0;
    stepDueAt = 0;
    nextActionTime = now + (unsigned long)random(300, 1200);
    drawEyesSmart(0, 60, false);
  }
}

void expressionModeTick() {
  const unsigned long now = millis();

  if (g_exprGroup == EXPR_GROUP_COCO) {
    // coco：旧 actionStep() 表情组
    if (!busy) {
      if (now < nextActionTime) return;
      startAction((int)random(0, (int)ACT_COUNT));
    }
    actionStep(now);
    return;
  }

  // bubu：渲染器差分表情组（base -> expr -> base -> gap -> next expr）
  static unsigned long lastFrameAt = 0;
  // 分支帧率：眨眼/转圈/哭/睡略快(80ms)；左右看/上下看/唱歌较慢(100ms)
  const unsigned long frameIntervalMs =
      (g_exprIdx == 0 || g_exprIdx == 2 || g_exprIdx == 4 || g_exprIdx == 7 || g_exprIdx == 8) ? 80UL
                                                                                                  : 100UL;
  if (lastFrameAt && (now - lastFrameAt) < frameIntervalMs) return;
  lastFrameAt = now;

  const bool bubuInSing = (g_exprIdx == 6 && g_exprPlaying);
  if (g_bubuSingLyricLatch && !bubuInSing) {
    clearSingOverlay();
    g_bubuSingLyricLatch = false;
  }

  if (!g_exprPlaying) {
    if (now < g_nextExprAt) return;
    g_exprPlaying = true;
    g_exprStartAt = now;
  }

  ExprFrame f = expr_base_frame_static();
  bool done = false;
  if (g_exprIdx == 0) {
    // Expression #1: blink both
    const unsigned long el = now - g_exprStartAt;
    const float t01 = (kBlinkDurMs > 0) ? (el / (float)kBlinkDurMs) : 1.0f;
    if (t01 >= 1.0f) {
      done = true;
    } else {
      const float k = blink_profile_local(t01);
      const float mul = (1.0f - 0.995f * k);
      f.openL = clamp01_local(f.openL * mul);
      f.openR = clamp01_local(f.openR * mul);
    }
  } else if (g_exprIdx == 1) {
    // Expression #2: look right -> left -> center
    const unsigned long el = now - g_exprStartAt;
    const float t01 = (kLookLRDurMs > 0) ? (el / (float)kLookLRDurMs) : 1.0f;
    if (t01 >= 1.0f) {
      done = true;
    } else {
      float gx = look_lr_profile_local(t01, 0.85f);
      // Coarser steps = fewer pupil redraws = less flash
      gx = quant_local(gx, 0.12f);
      f.gazeX = clamp_local(gx, -1.0f, 1.0f);
    }
  } else if (g_exprIdx == 2 || g_exprIdx == 8) {
    // Expression #3 / #9：瞳孔沿眼白内椭圆转一圈；#9 与 #3 仅 sin 符号相反（旋转方向相反）
    const unsigned long el = now - g_exprStartAt;
    const float t01 = (kCircleDurMs > 0) ? (el / (float)kCircleDurMs) : 1.0f;
    if (t01 >= 1.0f) {
      done = true;
    } else {
      const float t = clamp01_local(t01);
      const float ang = 6.2831853f * t;
      const float sinSign = (g_exprIdx == 8) ? 1.0f : -1.0f;

      // 轨迹：与眼白外轮廓同形（在屏幕位移空间里保持“同一椭圆比”）。
      // 目标：瞳孔外轮廓离眼白边缘最近处始终留 4px，看起来像在眼白内沿转动。
      // 先算出允许的中心位移（像素）：a = baseRx - pupilRx - margin，b = baseRy - pupilRy - margin，
      // 再换算成 gazeX/gazeY 振幅（renderer 里 gaze->像素系数分别是 0.35*baseRx 与 0.28*baseRy）。
      int16_t prx = 0, pry = 0;
      expression_pupil_radii_from_t(f.pupil, prx, pry);
      const float marginPx = 4.0f;
      const float aPx = max(0.0f, (float)kBaseRx - (float)prx - marginPx);
      // 关键：上下边界取“当前开口 eyeRyNow”，否则瞳孔会在上/下方越过眼白开口边缘。
      const float eyeRyNowPx = (float)max((int16_t)1, expression_eye_ry_from_open(f.openL));
      const float bPx = max(0.0f, eyeRyNowPx - (float)pry - marginPx);
      const float ax = clamp_local(aPx / ((float)kBaseRx * 0.35f), 0.0f, 1.0f);
      const float ay = clamp_local(bPx / ((float)kBaseRy * 0.28f), 0.0f, 1.0f);

      // 先把“斗鸡眼基线偏置”平滑关掉，让瞳孔回到真正中心，再执行内切轨迹，避免溢出眼白。
      const float centerLead = 0.14f; // 前 14% 用于回正中
      const float leadT = smoothstep_local(clamp01_local(t / centerLead));
      f.crossEyeT = 1.0f - leadT; // 1 -> 0

      // 首尾补轨迹：淡入/淡出到中心，避免从(0,0)突然跳到椭圆边界、或结束突然回到(0,0)。
      const float fade = 0.18f; // 前后 18% 做平滑过渡
      const float wIn = smoothstep_local(t / fade);
      const float wOut = smoothstep_local((1.0f - t) / fade);
      const float w = wIn * wOut; // 0 -> 1 -> 0

      float gx = cosf(ang) * ax * w;
      float gy = sinSign * sinf(ang) * ay * w;

      // Same anti-flicker strategy as #2, but finer steps for smoother ellipse.
      gx = quant_local(gx, 0.08f);
      gy = quant_local(gy, 0.08f);
      // Quantization may round outward; clamp back to the allowed inset-ellipse range for this frame.
      const float gxMax = ax * w;
      const float gyMax = ay * w;
      gx = clamp_local(gx, -gxMax, gxMax);
      gy = clamp_local(gy, -gyMax, gyMax);
      f.gazeX = clamp_local(gx, -1.0f, 1.0f);
      f.gazeY = clamp_local(gy, -1.0f, 1.0f);
    }
  } else if (g_exprIdx == 3) {
    // Expression #4: half-close both eyes for 3s, then open
    const unsigned long el = now - g_exprStartAt;
    const unsigned long total = kHalfCloseInMs + kHalfCloseHoldMs + kHalfCloseOutMs;
    if (total == 0 || el >= total) {
      done = true;
    } else {
      const float baseOpen = f.openL; // base frame uses same for L/R
      const float halfOpen = 0.24f;
      if (el < kHalfCloseInMs) {
        const float t = smoothstep_local((float)el / (float)max(1UL, kHalfCloseInMs));
        const float o = lerp_local(baseOpen, halfOpen, t);
        f.openL = clamp01_local(o);
        f.openR = clamp01_local(o);
      } else if (el < (kHalfCloseInMs + kHalfCloseHoldMs)) {
        f.openL = halfOpen;
        f.openR = halfOpen;
      } else {
        const unsigned long e2 = el - (kHalfCloseInMs + kHalfCloseHoldMs);
        const float t = smoothstep_local((float)e2 / (float)max(1UL, kHalfCloseOutMs));
        const float o = lerp_local(halfOpen, baseOpen, t);
        f.openL = clamp01_local(o);
        f.openR = clamp01_local(o);
      }
    }
  } else if (g_exprIdx == 4) {
    // Expression #5: cry — 紧闭（open=0）+ 眼下蓝竖条泪 + 伤心嘴
    const unsigned long el = now - g_exprStartAt;
    const unsigned long total = kCryInMs + kCryHoldMs + kCryOutMs;
    if (total == 0 || el >= total) {
      done = true;
    } else {
      const float baseOpen = f.openL;
      constexpr float kCryClosedOpen = 0.0f;
      if (el < kCryInMs) {
        const float t = smoothstep_local((float)el / (float)max(1UL, kCryInMs));
        const float o = lerp_local(baseOpen, kCryClosedOpen, t);
        f.openL = clamp01_local(o);
        f.openR = clamp01_local(o);
      } else if (el < (kCryInMs + kCryHoldMs)) {
        f.openL = kCryClosedOpen;
        f.openR = kCryClosedOpen;
      } else {
        const unsigned long e2 = el - (kCryInMs + kCryHoldMs);
        const float t = smoothstep_local((float)e2 / (float)max(1UL, kCryOutMs));
        const float o = lerp_local(kCryClosedOpen, baseOpen, t);
        f.openL = clamp01_local(o);
        f.openR = clamp01_local(o);
      }
      f.mouth = MouthId::SMILE_INVERTED; // 哭泣：向上凸弧嘴
      f.gazeX = 0.0f;
      f.gazeY = 0.16f;
    }
  } else if (g_exprIdx == 5 || g_exprIdx == 9) {
    // Expression #6 / #10：#6 为看下→上→中；#10 为看上→下→中（gazeY 取反）
    const unsigned long el = now - g_exprStartAt;
    const float t01 = (kLookUDDurMs > 0) ? (el / (float)kLookUDDurMs) : 1.0f;
    if (t01 >= 1.0f) {
      done = true;
    } else {
      float gy = look_lr_profile_local(t01, 0.85f);
      if (g_exprIdx == 9) gy = -gy;
      gy = quant_local(gy, 0.12f);
      f.gazeX = 0.0f;
      f.gazeY = clamp_local(gy, -1.0f, 1.0f);
    }
  } else if (g_exprIdx == 6) {
    // Expression #7: sing — 全睁 + O 嘴 + 斜向话筒（道具在 tick 后绘制）
    const unsigned long el = now - g_exprStartAt;
    const float t01 = (kSingDurMs > 0) ? (el / (float)kSingDurMs) : 1.0f;
    if (t01 >= 1.0f) {
      done = true;
    } else {
      f.gazeX = 0.52f;
      f.gazeY = 0.24f;
      f.pupil = 0.36f;
      f.mouthCxOfs = 14;
      // openL/openR 沿用 expr_base_frame_static() 全睁
      f.hidePupil = false;
      f.mouth = MouthId::SLEEP_O;
      {
        const float ph = (float)el * (6.2831853f / kSingMouthOCycleMs);
        const float w = 0.5f + 0.5f * sinf(ph);
        f.sleepMouthO = lerp_local(kSingMouthOMin, kSingMouthOMax, w);
      }
    }
  } else if (g_exprIdx == 7) {
    // Expression #8: sleep — slit + ZZZ
    const unsigned long el = now - g_exprStartAt;
    const unsigned long total = kSleepCloseMs + kSleepHoldMs + kSleepZzzPreOpenMs + kSleepOpenMs;
    if (total == 0 || el >= total) {
      done = true;
    } else {
      const float baseOpen = 0.62f;
      f.mouth = MouthId::SLEEP_O;
      {
        static float s_sleepMouthFilt = 0.5f;
        const float ph = (float)el * (6.2831853f / kSleepMouthOCycleMs);
        const float tgt = 0.5f + 0.5f * sinf(ph);
        // 每轮睡觉开头对齐目标，其余帧低通，减轻 O 大小变化过快带来的闪感
        if (el < 64UL) s_sleepMouthFilt = tgt;
        else s_sleepMouthFilt += (tgt - s_sleepMouthFilt) * 0.14f;
        f.sleepMouthO = s_sleepMouthFilt;
      }
      f.gazeX = 0.0f;
      f.gazeY = 0.0f;
      if (el < kSleepCloseMs) {
        const float t = smoothstep_local((float)el / (float)max(1UL, kSleepCloseMs));
        const float o = lerp_local(baseOpen, kSleepSlitOpen, t);
        f.openL = clamp01_local(o);
        f.openR = clamp01_local(o);
      } else if (el < kSleepCloseMs + kSleepHoldMs) {
        f.openL = kSleepSlitOpen;
        f.openR = kSleepSlitOpen;
      } else if (el < kSleepCloseMs + kSleepHoldMs + kSleepZzzPreOpenMs) {
        f.openL = kSleepSlitOpen;
        f.openR = kSleepSlitOpen;
      } else {
        const unsigned long e2 = el - (kSleepCloseMs + kSleepHoldMs + kSleepZzzPreOpenMs);
        const float t = smoothstep_local((float)e2 / (float)max(1UL, kSleepOpenMs));
        const float o = lerp_local(kSleepSlitOpen, baseOpen, t);
        f.openL = clamp01_local(o);
        f.openR = clamp01_local(o);
      }
      const float oMin = f.openL < f.openR ? f.openL : f.openR;
      f.hidePupil = (oMin <= kSleepHidePupilOpenMax);
    }
  }

  if (done) {
    const int prevExpr = g_exprIdx;
    const bool endingSleep = (g_exprIdx == 7);
    const bool endingCry = (g_exprIdx == 4);
    const bool endingSing = (g_exprIdx == 6);
    if (endingSing) {
      clearSingOverlay();
      g_bubuSingLyricLatch = false;
      g_exprRenderer.invalidateEyesAfterExternalClear();
      g_exprRenderer.clearMouthBoxToBg();
    }
    if (endingSleep) {
      bubuSleepZzzForeheadClear();
      g_exprRenderer.invalidateEyesAfterExternalClear();
      g_exprRenderer.clearMouthBoxToBg();
      g_bubuLastSleepEndedAt = now;
    }
    if (endingCry) {
      {
        const EyeGeom eL = expression_eye_left();
        const EyeGeom eR = expression_eye_right();
        bubuCryTearsClearPillarStrips(88, DISP_H, eL.cx, eR.cx, kCryClearW);
      }
      g_exprRenderer.invalidateEyesAfterExternalClear();
      g_exprRenderer.clearMouthBoxToBg();
    }
    g_exprRenderer.tick(expr_base_frame_static());
    g_exprPlaying = false;
    g_exprIdx = pickNextBubuExprIdx(prevExpr, now);
    g_nextExprAt = now + kExprGapMs;
    return;
  }

  if (g_exprIdx == 7 && g_exprPlaying) {
    const unsigned long el = now - g_exprStartAt;
    if (el >= kSleepCloseMs + kSleepHoldMs && el < kSleepCloseMs + kSleepHoldMs + kSleepZzzPreOpenMs) {
      bubuSleepZzzForeheadClear();
      g_exprRenderer.invalidateEyesAfterExternalClear();
    }
  }

  g_exprRenderer.tick(f);

  if (g_exprIdx == 4 && g_exprPlaying) {
    const unsigned long el = now - g_exprStartAt;
    drawBubuCryTears(f, el);
  }

  if (g_exprIdx == 7 && g_exprPlaying) {
    const unsigned long el = now - g_exprStartAt;
    initColoursOnce();
    if (el >= kSleepCloseMs && el < kSleepCloseMs + kSleepHoldMs) {
      drawBubuSleepZzz(el - kSleepCloseMs);
    }
  }

  // 唱歌：话筒每帧；歌词仅在进入本段画一次（嘴部已限制在 kExprLyricBandTopY 之上，见 expression_renderer）
  if (g_exprIdx == 6 && g_exprPlaying) {
    g_bubuSingLyricLatch = true;
    initColoursOnce();
    static unsigned long s_singLyricsForStart = 0xffffffffUL;
    static const char* s_singLyricChosen = nullptr;
    const bool singJustStarted = (s_singLyricsForStart != g_exprStartAt);
    if (singJustStarted) {
      s_singLyricsForStart = g_exprStartAt;
      const int pick = (int)random(0, kBubuSingLyricsCount);
      s_singLyricChosen = kBubuSingLyrics[pick];
    }
    drawSingMicOnce();
    if (singJustStarted && s_singLyricChosen) {
      showSingLyricsWrapped(s_singLyricChosen);
    }
  }
}

