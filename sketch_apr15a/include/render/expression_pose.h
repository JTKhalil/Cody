#pragma once

#include <Arduino.h>

// 说明：该模块只负责“生成每帧表情参数（pose）”，不直接绘制。
// 视觉常量需与 tools/expression-eye-preview.html 当前定稿保持一致。

struct ExpressionPose {
  float gazeX;   // [-1, 1]
  float gazeY;   // [-1, 1]
  float open;    // [0, 1]
  float blink;   // [0, 1]
  float pupil;   // [0, 1]
};

struct EyeGeom {
  int16_t cx;
  int16_t cy;
  int16_t rx; // baseRx
  int16_t ry; // baseRy
};

struct BBox {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

// --- Visual constants (LOCKED to preview) ---
static constexpr int16_t kDispW = 240;
static constexpr int16_t kDispH = 240;
static constexpr int16_t kEyeGap = 106;
static constexpr int16_t kEyeCy = 85;
// Eye size scaled +30%
static constexpr int16_t kBaseRx = 36;
static constexpr int16_t kBaseRy = 91;
static constexpr float kCrossBiasMul = 0.18f;   // baseRx * 0.18 * crossT
static constexpr float kBlinkClosedRy = 0.6f;   // <=0.6: closed line, pupil hidden

inline float clamp01f(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

EyeGeom expression_eye_left();
EyeGeom expression_eye_right();

// openEff = clamp01(open*(1-blink)); eyeRy = lerp(0, baseRy, openEff)
int16_t expression_eye_ry(const ExpressionPose& p);

// Helper: map an openness value [0,1] to eyeRy pixels (0..baseRy).
int16_t expression_eye_ry_from_open(float openEff);

// pupilRx = lerp(8,17,pupil); pupilRy = lerp(13,29,pupil)
void expression_pupil_radii(const ExpressionPose& p, int16_t& outRx, int16_t& outRy);

// Helper: map a pupil param [0,1] to pupil radii.
void expression_pupil_radii_from_t(float pupilT, int16_t& outRx, int16_t& outRy);

// Apply inward bias for cross-eye baseline (sideSign: -1 left, +1 right)
float expression_apply_cross_eye(float pupilCx, int sideSign, float gazeX, int16_t baseRx);

// Constrain pupil center to the inset ellipse computed from baseRx/baseRy and pupilRx/pupilRy:
// (dx/a)^2 + (dy/b)^2 <= 1, where a=baseRx-pupilRx, b=baseRy-pupilRy.
void expression_constrain_pupil_center(float& cx, float& cy, const EyeGeom& eye, int16_t pupilRx, int16_t pupilRy);

struct ExpressionState {
  uint32_t seed;
  unsigned long nextBlinkAt;
  unsigned long blinkStartAt;
  unsigned long blinkDurMs;
  bool blinking;
};

void expression_state_init(ExpressionState& st, uint32_t seed);
ExpressionPose expression_pose_generate(ExpressionState& st, unsigned long nowMs);

