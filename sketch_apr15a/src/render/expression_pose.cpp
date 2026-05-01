#include "include/render/expression_pose.h"

#include <math.h>

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

// 1D value noise in [-1,1], gridMs e.g. 250
static float noise1(unsigned long nowMs, uint32_t seed, unsigned long gridMs) {
  const unsigned long g = gridMs ? gridMs : 250;
  const unsigned long i0 = nowMs / g;
  const unsigned long i1 = i0 + 1;
  const float t = (nowMs % g) / (float)g;
  uint32_t s0 = hash_u32(seed ^ (uint32_t)i0);
  uint32_t s1 = hash_u32(seed ^ (uint32_t)i1);
  const float v0 = (rand01(s0) * 2.0f - 1.0f);
  const float v1 = (rand01(s1) * 2.0f - 1.0f);
  return v0 + (v1 - v0) * smoothstep(t);
}

EyeGeom expression_eye_left() {
  const int16_t cxL = (kDispW - kEyeGap) / 2;
  return EyeGeom{cxL, kEyeCy, kBaseRx, kBaseRy};
}

EyeGeom expression_eye_right() {
  const int16_t cxL = (kDispW - kEyeGap) / 2;
  return EyeGeom{(int16_t)(cxL + kEyeGap), kEyeCy, kBaseRx, kBaseRy};
}

int16_t expression_eye_ry(const ExpressionPose& p) {
  const float openEff = clamp01f(p.open * (1.0f - clamp01f(p.blink)));
  return expression_eye_ry_from_open(openEff);
}

int16_t expression_eye_ry_from_open(float openEff) {
  openEff = clamp01f(openEff);
  const float ry = lerpf(0.0f, (float)kBaseRy, openEff);
  return (int16_t)lroundf(ry);
}

void expression_pupil_radii(const ExpressionPose& p, int16_t& outRx, int16_t& outRy) {
  expression_pupil_radii_from_t(p.pupil, outRx, outRy);
}

void expression_pupil_radii_from_t(float pupilT, int16_t& outRx, int16_t& outRy) {
  const float t = clamp01f(pupilT);
  // Slightly larger pupil (static face prefers a fuller look)
  // Pupil scaled +30%
  outRx = (int16_t)lroundf(lerpf(13.0f, 27.0f, t));
  outRy = (int16_t)lroundf(lerpf(20.0f, 44.0f, t));
}

float expression_apply_cross_eye(float pupilCx, int sideSign, float gazeX, int16_t baseRx) {
  const float crossT0 = 1.0f - clampf(fabsf(gazeX), 0.0f, 1.0f);
  const float crossT = crossT0 * crossT0;
  const float bias = (float)baseRx * kCrossBiasMul * crossT;
  return pupilCx + (sideSign < 0 ? +bias : -bias);
}

void expression_constrain_pupil_center(float& cx, float& cy, const EyeGeom& eye, int16_t pupilRx, int16_t pupilRy) {
  const float a = fmaxf(0.5f, (float)eye.rx - (float)pupilRx);
  const float b = fmaxf(0.5f, (float)eye.ry - (float)pupilRy);
  float dx = cx - (float)eye.cx;
  float dy = cy - (float)eye.cy;
  const float v = (dx * dx) / (a * a) + (dy * dy) / (b * b);
  if (v > 1.0f) {
    const float s = 1.0f / sqrtf(v);
    dx *= s;
    dy *= s;
    cx = (float)eye.cx + dx;
    cy = (float)eye.cy + dy;
  }
}

static float blink_curve(float t01) {
  // A symmetric bump curve in [0,1].
  const float x = fabsf(2.0f * t01 - 1.0f);
  const float u = 1.0f - x;  // triangle
  return smoothstep(clampf(u, 0.0f, 1.0f));
}

void expression_state_init(ExpressionState& st, uint32_t seed) {
  st.seed = seed ? seed : 0xC0DEF00Du;
  st.blinking = false;
  st.blinkStartAt = 0;
  st.blinkDurMs = 160;
  st.nextBlinkAt = millis() + 1200 + (unsigned long)(rand01(st.seed) * 2400);
}

ExpressionPose expression_pose_generate(ExpressionState& st, unsigned long nowMs) {
  // Continuous driver: lightweight low-frequency noise.
  const float nx = 0.60f * noise1(nowMs, st.seed ^ 0x11u, 320) + 0.30f * noise1(nowMs, st.seed ^ 0x12u, 900);
  const float ny = 0.70f * noise1(nowMs, st.seed ^ 0x21u, 520) + 0.20f * noise1(nowMs, st.seed ^ 0x22u, 1400);
  const float no = 0.80f * noise1(nowMs, st.seed ^ 0x31u, 900) + 0.20f * noise1(nowMs, st.seed ^ 0x32u, 2300);
  const float np = 0.70f * noise1(nowMs, st.seed ^ 0x41u, 1600) + 0.30f * noise1(nowMs, st.seed ^ 0x42u, 3600);

  ExpressionPose p{};
  p.gazeX = clampf(nx * 0.9f, -1.0f, 1.0f);
  p.gazeY = clampf(ny * 0.7f, -1.0f, 1.0f);
  p.open = clamp01f(0.72f + 0.10f * no);
  p.pupil = clamp01f(0.55f + 0.10f * np);
  p.blink = 0.0f;

  // Blink scheduler
  if (!st.blinking && nowMs >= st.nextBlinkAt) {
    st.blinking = true;
    st.blinkStartAt = nowMs;
    st.blinkDurMs = 140 + (unsigned long)(rand01(st.seed) * 120);
  }
  if (st.blinking) {
    const unsigned long el = nowMs - st.blinkStartAt;
    const float t01 = (st.blinkDurMs > 0) ? (el / (float)st.blinkDurMs) : 1.0f;
    if (t01 >= 1.0f) {
      st.blinking = false;
      p.blink = 0.0f;
      st.nextBlinkAt = nowMs + 1200 + (unsigned long)(rand01(st.seed) * 3200);
    } else {
      p.blink = blink_curve(clampf(t01, 0.0f, 1.0f));
    }
  }

  return p;
}

