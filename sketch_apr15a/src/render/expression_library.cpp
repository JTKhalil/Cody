#include "include/render/expression_library.h"

#include <math.h>

static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float clamp01(float x) { return clampf(x, 0.0f, 1.0f); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

static uint32_t hash_u32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

static float rand01(uint32_t& s) {
  s = hash_u32(s + 0x9e3779b9U);
  return (s & 0x00FFFFFF) / 16777215.0f;
}

static float smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }
static float easeInOut(float t) { return smoothstep(clamp01(t)); }

static ExprStateId nextInOrder(ExprStateId id) {
  const int i = (int)id;
  const int n = (int)ExprStateId::COUNT;
  return (ExprStateId)((i + 1) % n);
}

static ExprFrame mixFrame(const ExprFrame& a, const ExprFrame& b, float t) {
  ExprFrame o = a;
  o.gazeX = lerpf(a.gazeX, b.gazeX, t);
  o.gazeY = lerpf(a.gazeY, b.gazeY, t);
  o.openL = lerpf(a.openL, b.openL, t);
  o.openR = lerpf(a.openR, b.openR, t);
  o.pupil = lerpf(a.pupil, b.pupil, t);
  o.crossEyeT = lerpf(a.crossEyeT, b.crossEyeT, t);
  o.hidePupil = (t < 0.5f) ? a.hidePupil : b.hidePupil;
  o.sleepMouthO = lerpf(a.sleepMouthO, b.sleepMouthO, t);
  o.mouthCxOfs = (int16_t)lroundf(lerpf((float)a.mouthCxOfs, (float)b.mouthCxOfs, t));
  // mouth/brow: during transition, keep "from" in first half then "to"
  o.mouth = (t < 0.5f) ? a.mouth : b.mouth;
  o.brow = (t < 0.5f) ? a.brow : b.brow;
  return o;
}

ExprFrame ExpressionLibrary::baseForState(ExprStateId id) const {
  ExprFrame f{};
  f.gazeX = 0.0f;
  f.gazeY = 0.0f;
  // 关键策略：尽量让眼睛“常态不变形”，把动态交给视线/嘴巴。
  // ST7789 上 eyeRy 频繁变化会显著增加闪烁观感。
  f.openL = 0.62f;
  f.openR = 0.62f;
  f.pupil = 0.55f;  // 固定瞳孔大小，减少闪烁/残影风险
  f.crossEyeT = 1.0f;
  f.hidePupil = false;
  f.mouth = MouthId::NEUTRAL;
  f.brow = BrowId::NEUTRAL;

  switch (id) {
    case ExprStateId::SMILE:
      f.mouth = MouthId::SMILE;
      f.brow = BrowId::SMILE;
      break;
    case ExprStateId::WINK:
      f.mouth = MouthId::SMIRK;
      f.brow = BrowId::WINK;
      break;
    case ExprStateId::LOOK_LR:
      f.mouth = MouthId::NEUTRAL;
      f.brow = BrowId::CURIOUS;
      break;
    case ExprStateId::LOOK_UD:
      f.mouth = MouthId::NEUTRAL;
      f.brow = BrowId::CURIOUS;
      break;
    case ExprStateId::SAD:
      f.gazeY = 0.35f;
      f.mouth = MouthId::FROWN;
      f.brow = BrowId::SAD;
      break;
    case ExprStateId::ANGRY:
      f.gazeY = 0.10f;
      f.mouth = MouthId::ANGRY;
      f.brow = BrowId::ANGRY;
      break;
    default:
      break;
  }
  return f;
}

static float pingPong(float t01) {
  // 0..1..0
  const float x = fmodf(t01, 1.0f);
  return (x < 0.5f) ? (x * 2.0f) : (2.0f - x * 2.0f);
}

static float sweepWithPauses(unsigned long tMs, unsigned long periodMs, unsigned long pauseMs) {
  // Sweep -1 -> +1 -> -1 with pauses at ends.
  const unsigned long half = periodMs / 2;
  const unsigned long seg = half + pauseMs;
  const unsigned long full = seg * 2;
  const unsigned long p = full ? (tMs % full) : 0;
  if (p < seg) {
    // left -> right then pause at right
    if (p < half) {
      const float t = (half > 0) ? (p / (float)half) : 1.0f;
      return lerpf(-1.0f, +1.0f, easeInOut(t));
    }
    return +1.0f;
  }
  // right -> left then pause at left
  const unsigned long p2 = p - seg;
  if (p2 < half) {
    const float t = (half > 0) ? (p2 / (float)half) : 1.0f;
    return lerpf(+1.0f, -1.0f, easeInOut(t));
  }
  return -1.0f;
}

static float winkPulse(unsigned long tMs, unsigned long startMs, unsigned long durMs) {
  if (tMs < startMs) return 0.0f;
  const unsigned long el = tMs - startMs;
  if (el >= durMs) return 0.0f;
  const float t01 = durMs ? (el / (float)durMs) : 1.0f;
  // bump: 0->1->0
  return pingPong(t01);
}

static float quantStep(float x, float step) {
  if (step <= 0.0f) return x;
  return step * lroundf(x / step);
}

void ExpressionLibrary::applyLoop(ExprStateId id, unsigned long nowMs, ExprFrame& io) const {
  const unsigned long t = nowMs - stateStart_;
  switch (id) {
    case ExprStateId::LOOK_LR: {
      const float v = sweepWithPauses(t, 5200, 800);
      io.gazeX = quantStep(0.90f * v, 0.08f);
      break;
    }
    case ExprStateId::LOOK_UD: {
      const float v = sweepWithPauses(t, 5200, 900);
      io.gazeY = quantStep(0.70f * v, 0.08f);
      break;
    }
    case ExprStateId::WINK: {
      // Two or three winks spread across >=6s.
      const float w1 = winkPulse(t, 1600, 800);
      const float w2 = winkPulse(t, 4200, 800);
      const float w3 = winkPulse(t, 6800, 800);
      const float w = clamp01(w1 + w2 + w3);
      // close right eye
      io.openR = io.openR * (1.0f - 0.95f * w);
      break;
    }
    case ExprStateId::ANGRY: {
      // very small shake
      const float s = sinf((float)t * 0.006f) * 0.06f;
      io.gazeX = clampf(io.gazeX + s, -1.0f, 1.0f);
      break;
    }
    default:
      break;
  }
}

void ExpressionLibrary::scheduleNext(unsigned long nowMs) {
  // hold >= 6s
  const unsigned long extra = (unsigned long)(rand01(seed_) * 6000.0f);
  holdMs_ = 6000 + extra;
  stateStart_ = nowMs;
  next_ = nextInOrder(cur_);
}

void ExpressionLibrary::startTransition(unsigned long nowMs) {
  inTrans_ = true;
  transStart_ = nowMs;
  transMs_ = 600;
  from_ = baseForState(cur_);
  to_ = baseForState(next_);
  // loop effects are applied after interpolation
}

void ExpressionLibrary::enter(uint32_t seed) {
  seed_ = seed ? seed : 0xC0DEF00Du;
  inited_ = true;
  cur_ = ExprStateId::SMILE;
  next_ = ExprStateId::WINK;
  stateStart_ = millis();
  scheduleNext(stateStart_);
  inTrans_ = false;

  // 全局眨眼关闭：不再调度
  blinking_ = false;
  blinkStart_ = 0;
  blinkDur_ = 0;
  nextBlinkAt_ = 0;
}

void ExpressionLibrary::tick(unsigned long nowMs, ExprFrame& out) {
  if (!inited_) enter((uint32_t)(nowMs ^ 0xC0DEF00Du));

  // transition trigger when hold elapsed
  if (!inTrans_ && (nowMs - stateStart_) >= holdMs_) {
    startTransition(nowMs);
  }

  // compute frame
  ExprFrame f{};
  if (inTrans_) {
    const unsigned long el = nowMs - transStart_;
    const float t01 = transMs_ ? (el / (float)transMs_) : 1.0f;
    const float t = easeInOut(t01);
    f = mixFrame(from_, to_, clamp01(t));
    // apply loop of destination gradually (small)
    applyLoop(cur_, nowMs, f);
    applyLoop(next_, nowMs, f);
    if (t01 >= 1.0f) {
      inTrans_ = false;
      cur_ = next_;
      scheduleNext(nowMs);
    }
  } else {
    f = baseForState(cur_);
    applyLoop(cur_, nowMs, f);
  }

  // 全局眨眼：为了“彻底压闪”，这里直接关闭。
  // 需要眨眼时由 WINK 表情单独驱动（且只动一只眼的局部区域）。

  // clamp
  f.gazeX = clampf(f.gazeX, -1.0f, 1.0f);
  f.gazeY = clampf(f.gazeY, -1.0f, 1.0f);
  f.openL = clamp01(f.openL);
  f.openR = clamp01(f.openR);
  f.pupil = clamp01(f.pupil);
  out = f;
}

