#include "include/render/expression_renderer.h"

#include "include/globals.h"

#include <math.h>

void ExpressionRenderer::initColoursOnce() {
  if (inited_) return;
  // 使用原表情模式背景橙（与旧 expression_mode.cpp 一致）
  bg_ = tft.color565(218, 17, 0);
  white_ = ST77XX_WHITE;
  // 更深的蓝色瞳孔
  blue_ = tft.color565(0, 90, 255);
  inited_ = true;
}

BBox ExpressionRenderer::eyeMaxBBox(const EyeGeom& eye, int margin) {
  const int16_t x = (int16_t)(eye.cx - eye.rx - margin);
  const int16_t y = (int16_t)(eye.cy - eye.ry - margin);
  const int16_t w = (int16_t)(2 * (eye.rx + margin));
  const int16_t h = (int16_t)(2 * (eye.ry + margin));
  return BBox{x, y, w, h};
}

void ExpressionRenderer::restoreBox(const BBox& b) {
  if (b.w <= 0 || b.h <= 0) return;
  tft.fillRect(b.x, b.y, b.w, b.h, bg_);
}

void ExpressionRenderer::fillEllipse(int16_t cx, int16_t cy, int16_t rx, int16_t ry, uint16_t col) {
  if (rx <= 0 || ry <= 0) return;
  const float frx = (float)rx;
  const float fry = (float)ry;
  for (int16_t y = -ry; y <= ry; y++) {
    const float fy = (float)y;
    const float term = 1.0f - (fy * fy) / (fry * fry);
    if (term <= 0.0f) continue;
    const int16_t x = (int16_t)lroundf(frx * sqrtf(term));
    tft.drawFastHLine((int16_t)(cx - x), (int16_t)(cy + y), (int16_t)(2 * x + 1), col);
  }
}

void ExpressionRenderer::fillEllipseClippedToEye(int16_t cx, int16_t cy, int16_t rx, int16_t ry,
                                                const EyeGeom& eye, int16_t eyeRy, uint16_t col) {
  // Clip pupil ellipse by the current eye opening ellipse (eye.rx, eyeRy).
  if (rx <= 0 || ry <= 0) return;
  if (eyeRy <= 0) return;
  const int16_t yMin = (int16_t)max((int)(cy - ry), (int)(eye.cy - eyeRy));
  const int16_t yMax = (int16_t)min((int)(cy + ry), (int)(eye.cy + eyeRy));
  // 半闭眼：眼洞最上一行画瞳孔易与上睑形成 1px 深色接缝，顶行留白由眼白底承担
  int16_t y0 = yMin;
  if (col == blue_ && eyeRy < 48 && eyeRy > 4) {
    const int16_t firstBlueY = (int16_t)(eye.cy - eyeRy + 1);
    if (y0 < firstBlueY) y0 = firstBlueY;
  }
  if (y0 > yMax) return;
  const float frx = (float)rx;
  const float fry = (float)ry;
  const float eRx = (float)eye.rx;
  const float eRy = (float)eyeRy;
  for (int16_t y = y0; y <= yMax; y++) {
    const float dyP = (float)(y - cy);
    const float dyE = (float)(y - eye.cy);
    const float tp = 1.0f - (dyP * dyP) / (fry * fry);
    const float te = 1.0f - (dyE * dyE) / (eRy * eRy);
    if (tp <= 0.0f || te <= 0.0f) continue;
    const int16_t xP = (int16_t)lroundf(frx * sqrtf(tp));
    const int16_t xE = (int16_t)lroundf(eRx * sqrtf(te));
    const int16_t x = (int16_t)min((int)xP, (int)xE);
    tft.drawFastHLine((int16_t)(cx - x), y, (int16_t)(2 * x + 1), col);
  }
}

void ExpressionRenderer::applyPupilMoveDeltaClipped(int16_t cx0, int16_t cy0, int16_t rx0, int16_t ry0,
                                                     int16_t cx1, int16_t cy1, int16_t rx1, int16_t ry1,
                                                     const EyeGeom& eye, int16_t eyeRy) {
  if (eyeRy <= 0) return;
  if (rx0 <= 0 || ry0 <= 0 || rx1 <= 0 || ry1 <= 0) return;
  const float eRx = (float)eye.rx;
  const float eRy = (float)eyeRy;
  const int16_t eyeTopY = (int16_t)(eye.cy - eyeRy);
  const bool squintSkipTopBlue = (eyeRy < 48 && eyeRy > 4);

  auto halfClip = [&](int16_t cx, int16_t cy, int16_t rx, int16_t ry, int16_t y) -> int16_t {
    if (rx <= 0 || ry <= 0) return 0;
    const float dyP = (float)(y - cy);
    const float dyE = (float)(y - eye.cy);
    const float tp = 1.0f - (dyP * dyP) / ((float)ry * (float)ry);
    const float te = 1.0f - (dyE * dyE) / (eRy * eRy);
    if (tp <= 0.0f || te <= 0.0f) return 0;
    const int16_t xP = (int16_t)lroundf((float)rx * sqrtf(tp));
    const int16_t xE = (int16_t)lroundf(eRx * sqrtf(te));
    return (int16_t)min((int)xP, (int)xE);
  };

  const int16_t y0Min = (int16_t)max((int)(cy0 - ry0), (int)(eye.cy - eyeRy));
  const int16_t y0Max = (int16_t)min((int)(cy0 + ry0), (int)(eye.cy + eyeRy));
  const int16_t y1Min = (int16_t)max((int)(cy1 - ry1), (int)(eye.cy - eyeRy));
  const int16_t y1Max = (int16_t)min((int)(cy1 + ry1), (int)(eye.cy + eyeRy));
  const int16_t yMin = (int16_t)min((int)y0Min, (int)y1Min);
  const int16_t yMax = (int16_t)max((int)y0Max, (int)y1Max);

  for (int16_t y = yMin; y <= yMax; y++) {
    const int16_t w0 = halfClip(cx0, cy0, rx0, ry0, y);
    const int16_t w1 = halfClip(cx1, cy1, rx1, ry1, y);
    if (w0 <= 0 && w1 <= 0) continue;

    if (w0 > 0 && w1 > 0) {
      const int16_t l0 = (int16_t)(cx0 - w0);
      const int16_t r0 = (int16_t)(cx0 + w0);
      const int16_t l1 = (int16_t)(cx1 - w1);
      const int16_t r1 = (int16_t)(cx1 + w1);
      // White = old \ new on this scanline (symmetric-diff cleanup; avoid full union white flash).
      if (r0 < l1 || r1 < l0) {
        tft.drawFastHLine(l0, y, (int16_t)(r0 - l0 + 1), white_);
      } else {
        if (l0 < l1) {
          const int16_t e = (int16_t)min((int)r0, (int)l1 - 1);
          if (e >= l0) tft.drawFastHLine(l0, y, (int16_t)(e - l0 + 1), white_);
        }
        if (r0 > r1) {
          const int16_t s = (int16_t)max((int)l0, (int)r1 + 1);
          if (s <= r0) tft.drawFastHLine(s, y, (int16_t)(r0 - s + 1), white_);
        }
      }
      if ((!squintSkipTopBlue || y != eyeTopY) && eyeRy > 1) tft.drawFastHLine(l1, y, (int16_t)(2 * w1 + 1), blue_);
    } else if (w0 > 0) {
      const int16_t l0 = (int16_t)(cx0 - w0);
      const int16_t r0 = (int16_t)(cx0 + w0);
      tft.drawFastHLine(l0, y, (int16_t)(r0 - l0 + 1), white_);
    } else {
      const int16_t l1 = (int16_t)(cx1 - w1);
      if ((!squintSkipTopBlue || y != eyeTopY) && eyeRy > 1) tft.drawFastHLine(l1, y, (int16_t)(2 * w1 + 1), blue_);
    }
  }
}

void ExpressionRenderer::drawClosedEyeLine(const EyeGeom& eye) {
  // 闭眼横线：勿在端点画 fillCircle(r=2)，圆心在外角点时会向左右各凸出到脸皮上形成白噪点。
  const int16_t y = eye.cy;
  const int16_t x0 = (int16_t)(eye.cx - eye.rx);
  const int16_t span = (int16_t)(2 * eye.rx + 1); // inclusive [cx-rx, cx+rx]
  tft.drawFastHLine(x0, y, span, white_);
  tft.drawFastHLine(x0, (int16_t)(y + 1), span, white_);
}

void ExpressionRenderer::enter() {
  initColoursOnce();
  tft.fillScreen(bg_);
  hasPrev_ = false;
  prevEyeRyL_ = -1;
  prevEyeRyR_ = -1;
  prevClosedL_ = false;
  prevClosedR_ = false;
  hasFacePrev_ = false;
  prevMouth_ = MouthId::NEUTRAL;
  prevMouthCxOfs_ = -32768;
  prevSleepOrxDraw_ = -32768;
  prevSleepOryDraw_ = -32768;
  prevPupilRxL_ = prevPupilRyL_ = 0;
  prevPupilRxR_ = prevPupilRyR_ = 0;
  prevHidePupil_ = false;
}

void ExpressionRenderer::invalidateEyesAfterExternalClear() {
  prevEyeRyL_ = -1;
  prevEyeRyR_ = -1;
}

static inline int16_t ellipseXExtent(int16_t rx, int16_t ry, int16_t yAbs) {
  if (rx <= 0 || ry <= 0) return 0;
  if (yAbs >= ry) return 0;
  const float frx = (float)rx;
  const float fry = (float)ry;
  const float fy = (float)yAbs;
  const float term = 1.0f - (fy * fy) / (fry * fry);
  if (term <= 0.0f) return 0;
  return (int16_t)lroundf(frx * sqrtf(term));
}

// hidePupil 眯眼缝：上下「帽区」仍在全眼椭圆内，差分容易留白点 —— 按外轮廓刷回脸皮色
static void paintSlitCapsBg(Adafruit_ST7789& tft, uint16_t bg, const EyeGeom& eye, int16_t slitRy) {
  if (slitRy <= 0 || slitRy > 12) return;
  const int16_t yr = eye.ry;
  for (int16_t yy = (int16_t)(eye.cy - yr); yy <= (int16_t)(eye.cy - slitRy - 1); yy++) {
    const int16_t yAbs = (int16_t)abs((int)yy - eye.cy);
    if (yAbs >= yr) continue;
    const int16_t xe = ellipseXExtent(eye.rx, yr, yAbs);
    if (xe > 0) tft.drawFastHLine((int16_t)(eye.cx - xe), yy, (int16_t)(2 * xe + 1), bg);
  }
  for (int16_t yy = (int16_t)(eye.cy + slitRy + 1); yy <= (int16_t)(eye.cy + yr); yy++) {
    const int16_t yAbs = (int16_t)abs((int)yy - eye.cy);
    if (yAbs >= yr) continue;
    const int16_t xe = ellipseXExtent(eye.rx, yr, yAbs);
    if (xe > 0) tft.drawFastHLine((int16_t)(eye.cx - xe), yy, (int16_t)(2 * xe + 1), bg);
  }
}

void ExpressionRenderer::diffEyeWhite(const EyeGeom& eye, int16_t oldRy, int16_t newRy) {
  // Update only delta segments where the visible eye ellipse expands/shrinks.
  oldRy = (oldRy < 0) ? 0 : oldRy;
  newRy = (newRy < 0) ? 0 : newRy;
  const int16_t maxRy = (int16_t)max((int)oldRy, (int)newRy);
  if (maxRy <= 0) return;

  for (int16_t dy = -maxRy; dy <= maxRy; dy++) {
    const int16_t yAbsOld = (int16_t)abs(dy);
    const int16_t yAbsNew = yAbsOld;
    const int16_t xOld = ellipseXExtent(eye.rx, oldRy, yAbsOld);
    const int16_t xNew = ellipseXExtent(eye.rx, newRy, yAbsNew);
    if (xOld == xNew) continue;
    const int16_t y = (int16_t)(eye.cy + dy);
    if (xNew > xOld) {
      // Expand: paint new white edges only.
      // IMPORTANT: when expanding from a fully-closed line (xOld==0),
      // the generic "edges only" logic would skip the center pixel (x=cx),
      // leaving a vertical orange line through the eye.
      if (xOld == 0) {
        tft.drawFastHLine((int16_t)(eye.cx - xNew), y, (int16_t)(2 * xNew + 1), white_);
        continue;
      }
      const int16_t leftStart = (int16_t)(eye.cx - xNew);
      const int16_t leftEnd = (int16_t)(eye.cx - xOld - 1);
      if (leftEnd >= leftStart) tft.drawFastHLine(leftStart, y, (int16_t)(leftEnd - leftStart + 1), white_);

      const int16_t rightStart = (int16_t)(eye.cx + xOld + 1);
      const int16_t rightEnd = (int16_t)(eye.cx + xNew);
      if (rightEnd >= rightStart) tft.drawFastHLine(rightStart, y, (int16_t)(rightEnd - rightStart + 1), white_);
    } else {
      // Shrink: restore old white edges back to bg.
      // IMPORTANT: when the new ellipse has no pixels on this scanline (|dy| >= newRy),
      // we must clear the full old span including the center pixel; otherwise a vertical
      // line at x=cx can remain during blinks.
      if (yAbsOld >= newRy) {
        if (xOld > 0) tft.drawFastHLine((int16_t)(eye.cx - xOld), y, (int16_t)(2 * xOld + 1), bg_);
        else tft.drawFastHLine(eye.cx, y, 1, bg_);
        continue;
      }
      const int16_t leftStart = (int16_t)(eye.cx - xOld);
      const int16_t leftEnd = (int16_t)(eye.cx - xNew - 1);
      if (leftEnd >= leftStart) tft.drawFastHLine(leftStart, y, (int16_t)(leftEnd - leftStart + 1), bg_);

      const int16_t rightStart = (int16_t)(eye.cx + xNew + 1);
      const int16_t rightEnd = (int16_t)(eye.cx + xOld);
      if (rightEnd >= rightStart) tft.drawFastHLine(rightStart, y, (int16_t)(rightEnd - rightStart + 1), bg_);
    }
  }
}

void ExpressionRenderer::clearMouthBoxToBg() {
  initColoursOnce();
  tft.fillRect(mouthBox_.x, mouthBox_.y, mouthBox_.w, mouthBox_.h, bg_);
  prevMouthCxOfs_ = -32768;
  prevSleepOrxDraw_ = -32768;
  prevSleepOryDraw_ = -32768;
}

void ExpressionRenderer::drawMouth(const ExprFrame& frame, bool force) {
  const MouthId id = frame.mouth;
  initColoursOnce();

  if (id != MouthId::SLEEP_O) {
    prevSleepOrxDraw_ = -32768;
    prevSleepOryDraw_ = -32768;
  }

  // 睡觉 O 嘴：量化尺寸 + 仅在半径变化时清底重画，避免每帧整区橙闪；外实内空椭圆，环更厚
  if (id == MouthId::SLEEP_O) {
    const int16_t cx = (int16_t)(120 + frame.mouthCxOfs);
    const int16_t y = 172;
    const uint16_t col = ST77XX_BLACK;
    const float ocx = (float)cx;
    const float ocy = (float)(y + 6);
    float t01 = clampf(frame.sleepMouthO, 0.0f, 1.0f);
    constexpr int kSleepOQuantSteps = 14;
    t01 = floorf(t01 * (float)(kSleepOQuantSteps - 1) + 0.5f) / (float)(kSleepOQuantSteps - 1);
    const float orx = lerpf(9.0f, 19.0f, t01);
    const float ory = lerpf(7.0f, 15.0f, t01);
    const int16_t icx = (int16_t)lroundf(ocx);
    const int16_t icy = (int16_t)lroundf(ocy);
    int16_t orxI = (int16_t)lroundf(orx);
    int16_t oryI = (int16_t)lroundf(ory);
    const int16_t wall = 6;
    if (orxI < 2) orxI = 2;
    if (oryI < 2) oryI = 2;

    if (!force && prevMouth_ == MouthId::SLEEP_O && orxI == prevSleepOrxDraw_ && oryI == prevSleepOryDraw_ &&
        frame.mouthCxOfs == prevMouthCxOfs_) {
      return;
    }

    // 仅清 O 嘴附近，避免整张 mouthBox（右侧与话筒重叠）每帧铺橙导致话筒闪屏
    const int16_t oldOrx = prevSleepOrxDraw_;
    const int16_t oldOry = prevSleepOryDraw_;
    const bool tightSleepOClear = (prevMouth_ == MouthId::SLEEP_O && oldOrx >= 0 && oldOry >= 0 &&
                                   frame.mouthCxOfs == prevMouthCxOfs_);
    if (tightSleepOClear) {
      const int16_t uRx = (int16_t)max((int)orxI, (int)oldOrx);
      const int16_t uRy = (int16_t)max((int)oryI, (int)oldOry);
      constexpr int16_t kRingPad = 10;
      const int16_t halfW = (int16_t)(uRx + wall + kRingPad);
      const int16_t halfH = (int16_t)(uRy + wall + kRingPad);
      int16_t x0 = (int16_t)(icx - halfW);
      int16_t y0 = (int16_t)(icy - halfH);
      int16_t rw = (int16_t)(2 * halfW + 1);
      int16_t rh = (int16_t)(2 * halfH + 1);
      if (x0 < 0) {
        rw = (int16_t)(rw + x0);
        x0 = 0;
      }
      if (y0 < 0) {
        rh = (int16_t)(rh + y0);
        y0 = 0;
      }
      if ((int32_t)x0 + rw > kDispW) rw = (int16_t)(kDispW - x0);
      if ((int32_t)y0 + rh > kDispH) rh = (int16_t)(kDispH - y0);
      if (rw > 0 && rh > 0) tft.fillRect(x0, y0, rw, rh, bg_);
    } else {
      tft.fillRect(mouthBox_.x, mouthBox_.y, mouthBox_.w, mouthBox_.h, bg_);
    }

    prevSleepOrxDraw_ = orxI;
    prevSleepOryDraw_ = oryI;
    prevMouthCxOfs_ = frame.mouthCxOfs;
    hasFacePrev_ = true;
    prevMouth_ = MouthId::SLEEP_O;

    fillEllipse(icx, icy, orxI, oryI, col);
    const int16_t irx = (int16_t)(orxI - wall);
    const int16_t iry = (int16_t)(oryI - wall);
    if (irx > 0 && iry > 0) fillEllipse(icx, icy, irx, iry, bg_);
    return;
  }

  // 默认只有 mouth 变化才重画（减少闪烁）
  // 但当眼睛区域更新可能覆盖到嘴巴上方时，需要强制重画一次。
  if (!force && hasFacePrev_ && id == prevMouth_ && frame.mouthCxOfs == prevMouthCxOfs_) return;
  // 嘴型切换或嘴水平偏移变化时必须先铺回脸皮色，否则黑线叠黑线会缺笔画
  if (hasFacePrev_ && (id != prevMouth_ || frame.mouthCxOfs != prevMouthCxOfs_)) {
    tft.fillRect(mouthBox_.x, mouthBox_.y, mouthBox_.w, mouthBox_.h, bg_);
  }
  hasFacePrev_ = true;
  prevMouth_ = id;
  prevMouthCxOfs_ = frame.mouthCxOfs;

  // 方案 A：平时不重画；仅在切换时用 mouthBox_ 局部清底，再只用黑色笔画覆盖。
  const int16_t cx = (int16_t)(120 + frame.mouthCxOfs);
  const int16_t y = 172;
  const uint16_t col = ST77XX_BLACK;

  // 厚线（10px）+ 圆角：沿线段/曲线采样画一串小圆点，天然圆头/圆角。
  const int16_t r = 3; // thickness=6
  auto thickLine = [&](float x0, float y0, float x1, float y1) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float dist = sqrtf(dx * dx + dy * dy);
    // 更密的采样，边缘更圆润（避免颗粒感）
    const int steps = (int)max(1.0f, dist * 2.2f);
    for (int i = 0; i <= steps; i++) {
      const float t = (steps > 0) ? (i / (float)steps) : 1.0f;
      const int16_t x = (int16_t)lroundf(x0 + dx * t);
      const int16_t yy = (int16_t)lroundf(y0 + dy * t);
      tft.fillCircle(x, yy, r, col);
    }
  };

  auto thickQuad = [&](float x0, float y0, float cx1, float cy1, float x1, float y1) {
    const int steps = 84;
    float px = x0, py = y0;
    for (int i = 1; i <= steps; i++) {
      const float t = i / (float)steps;
      const float u = 1.0f - t;
      const float x = u * u * x0 + 2.0f * u * t * cx1 + t * t * x1;
      const float yy = u * u * y0 + 2.0f * u * t * cy1 + t * t * y1;
      thickLine(px, py, x, yy);
      px = x;
      py = yy;
    }
  };

  switch (id) {
    case MouthId::NEUTRAL:
      thickLine(cx - 28, y, cx + 28, y);
      break;
    case MouthId::SMILE:
      // 5-seg arc up
      thickLine(cx - 30, y + 6, cx - 15, y + 2);
      thickLine(cx - 15, y + 2, cx, y);
      thickLine(cx, y, cx + 15, y + 2);
      thickLine(cx + 15, y + 2, cx + 30, y + 6);
      break;
    case MouthId::SMILE_INVERTED: {
      // 哭泣：向上凸弧（二次贝塞尔）。对称时 t=0.5 纵坐标为 (y角+y控)/2，控制点上移才明显上凸。
      const float xL = (float)(cx - 30);
      const float xR = (float)(cx + 30);
      const float yCorner = (float)(y + 6);
      const float yCtrl = (float)(y - 5); // 与 yCorner 中点约 y+0.5，弧顶靠近原 SMILE 中心
      thickQuad(xL, yCorner, (float)cx, yCtrl, xR, yCorner);
      break;
    }
    case MouthId::FROWN:
      // 5-seg arc down
      thickLine(cx - 30, y, cx - 15, y + 4);
      thickLine(cx - 15, y + 4, cx, y + 6);
      thickLine(cx, y + 6, cx + 15, y + 4);
      thickLine(cx + 15, y + 4, cx + 30, y);
      break;
    case MouthId::ANGRY:
      // zigzag
      thickLine(cx - 28, y + 4, cx - 14, y);
      thickLine(cx - 14, y, cx, y + 4);
      thickLine(cx, y + 4, cx + 14, y);
      thickLine(cx + 14, y, cx + 28, y + 4);
      break;
    case MouthId::SMIRK:
      // one-side up
      thickLine(cx - 28, y + 4, cx - 10, y + 2);
      thickLine(cx - 10, y + 2, cx + 12, y);
      thickLine(cx + 12, y, cx + 28, y + 2);
      break;
    case MouthId::WAVY:
      // “W”形：用 Catmull-Rom 样条穿过关键点，保证一条连续平滑曲线（无拐角感）
      {
        auto catmull = [&](float p0, float p1, float p2, float p3, float t) -> float {
          const float t2 = t * t;
          const float t3 = t2 * t;
          return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                         (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
        };

        // 宽度收窄（整体更像你截图的嘴）
        const float xL = (float)(cx - 36);
        const float xR = (float)(cx + 36);

        const float x1 = lerpf(xL, xR, 0.25f);
        const float x2 = lerpf(xL, xR, 0.50f);
        const float x3 = lerpf(xL, xR, 0.75f);

        const float yTop = (float)(y + 3);
        const float yPeak = (float)(y + 7);
        const float yLow = (float)(y + 13);

        // 5 个点：top -> low -> peak -> low -> top
        float px[5] = {xL, x1, x2, x3, xR};
        float py[5] = {yTop, yLow, yPeak, yLow, yTop};

        // 采样绘制：每段 0..1
        const int segSteps = 36;
        float prevX = px[0];
        float prevY = py[0];
        for (int seg = 0; seg < 4; seg++) {
          // endpoint padding
          const float p0x = (seg == 0) ? px[0] : px[seg - 1];
          const float p0y = (seg == 0) ? py[0] : py[seg - 1];
          const float p1x = px[seg];
          const float p1y = py[seg];
          const float p2x = px[seg + 1];
          const float p2y = py[seg + 1];
          const float p3x = (seg + 2 >= 5) ? px[4] : px[seg + 2];
          const float p3y = (seg + 2 >= 5) ? py[4] : py[seg + 2];

          for (int i = 1; i <= segSteps; i++) {
            const float t = i / (float)segSteps;
            const float x = catmull(p0x, p1x, p2x, p3x, t);
            const float yy = catmull(p0y, p1y, p2y, p3y, t);
            thickLine(prevX, prevY, x, yy);
            prevX = x;
            prevY = yy;
          }
        }
      }
      break;
    default:
      thickLine(cx - 28, y, cx + 28, y);
      break;
  }
}

// 眉毛已按需求移除（不再绘制）

void ExpressionRenderer::tick(const ExprFrame& frame) {
  initColoursOnce();
  const bool hidePupil = frame.hidePupil;
  const bool hidePupilChanged = (prevHidePupil_ != hidePupil);

  const EyeGeom L = expression_eye_left();
  const EyeGeom R = expression_eye_right();

  const int16_t eyeRyL = expression_eye_ry_from_open(frame.openL);
  const int16_t eyeRyR = expression_eye_ry_from_open(frame.openR);

  int16_t pupilRx, pupilRy;
  expression_pupil_radii_from_t(frame.pupil, pupilRx, pupilRy);
  const int16_t prX = (int16_t)min((int)pupilRx, (int)((int)L.rx * 72 / 100));
  const int16_t prY = (int16_t)min((int)pupilRy, (int)((int)L.ry * 78 / 100));
  const int16_t oldPrX = (hasPrev_ && prevPupilRx_ > 0) ? prevPupilRx_ : prX;
  const int16_t oldPrY = (hasPrev_ && prevPupilRy_ > 0) ? prevPupilRy_ : prY;

  auto drawOne = [&](const EyeGeom& eye,
                     int sideSign,
                     int16_t eyeRyNow,
                     int16_t& prevEyeRy,
                     bool& prevClosed,
                     int16_t& prevPCx,
                     int16_t& prevPCy,
                     int16_t& prevPrX,
                     int16_t& prevPrY) {
    // Compute current pupil center (full-open constraint; position not affected by eyeRy).
    float pcxF = (float)eye.cx + clampf(frame.gazeX, -1.0f, 1.0f) * (float)eye.rx * 0.35f;
    float pcyF = (float)eye.cy + clampf(frame.gazeY, -1.0f, 1.0f) * (float)eye.ry * 0.28f;
    // Cross-eye baseline bias (optional; some expressions want true geometric centering).
    const float crossT = clampf(frame.crossEyeT, 0.0f, 1.0f);
    if (crossT > 0.0f) {
      const float biased = expression_apply_cross_eye(pcxF, sideSign, frame.gazeX, eye.rx);
      pcxF = pcxF + (biased - pcxF) * crossT;
    }
    expression_constrain_pupil_center(pcxF, pcyF, eye, prX, prY);
    const int16_t pcx = (int16_t)lroundf(pcxF);
    const int16_t pcy = (int16_t)lroundf(pcyF);

    // First frame or state transition: do a local full redraw of this eye region.
    const bool first = !hasPrev_ || prevEyeRy < 0;
    const bool closedWas = prevClosed;
    const bool closedIs = (eyeRyNow <= (int16_t)lroundf(kBlinkClosedRy));
    const bool pupilSizeJump = (abs((int)prX - (int)oldPrX) >= 3) || (abs((int)prY - (int)oldPrY) >= 3);
    // NOTE: avoid full restore on open<->closed transitions; it causes visible flashes.
    if (first || pupilSizeJump) {
      if (closedIs) {
        drawClosedEyeLine(eye);
      } else {
        fillEllipse(eye.cx, eye.cy, eye.rx, eyeRyNow, white_);
        if (!hidePupil && eyeRyNow > 1) fillEllipseClippedToEye(pcx, pcy, prX, prY, eye, eyeRyNow, blue_);
        if (hidePupil) paintSlitCapsBg(tft, bg_, eye, eyeRyNow);
      }
      prevEyeRy = eyeRyNow;
      prevClosed = closedIs;
      prevPCx = pcx;
      prevPCy = pcy;
      prevPrX = prX;
      prevPrY = prY;
      return;
    }

    // No pixel changes -> skip draw entirely (big flicker win).
    if (!hidePupilChanged && prevEyeRy == eyeRyNow && prevPCx == pcx && prevPCy == pcy && prevPrX == prX &&
        prevPrY == prY) {
      return;
    }

    // 从闭眼细线 → 睁开：原先用 bg 铺整条横线清闭合线，会在眼区闪大块橙色（哭完睁眼尤其明显）。
    // 改为直接按当前 eyeRy 画完整眼白 + 瞳孔，再走后续帧的 diffEyeWhite 增量。
    if (closedWas && !closedIs) {
      fillEllipse(eye.cx, eye.cy, eye.rx, eyeRyNow, white_);
      if (!hidePupil && eyeRyNow > 1) fillEllipseClippedToEye(pcx, pcy, prX, prY, eye, eyeRyNow, blue_);
      if (hidePupil) paintSlitCapsBg(tft, bg_, eye, eyeRyNow);
      prevEyeRy = eyeRyNow;
      prevClosed = closedIs;
      prevPCx = pcx;
      prevPCy = pcy;
      prevPrX = prX;
      prevPrY = prY;
      return;
    }

    // 1) Update eye white edges only if eyeRy changed.
    if (prevEyeRy != eyeRyNow) {
      diffEyeWhite(eye, prevEyeRy, eyeRyNow);
    }

    // 2) Closed / pupil draw (minimize draw calls for less flicker)
    if (closedIs) {
      // Fully closed: draw the closed-eye line and do not draw pupil.
      drawClosedEyeLine(eye);
    } else {
      if (hidePupil) paintSlitCapsBg(tft, bg_, eye, eyeRyNow);

      const bool pupilGeomChanged = (prevPCx != pcx) || (prevPCy != pcy) || (prevPrX != prX) || (prevPrY != prY);
      const bool opening = (eyeRyNow > prevEyeRy);
      const bool pupilOnlyMove =
          (!closedWas && !closedIs && (prevEyeRy == eyeRyNow) && pupilGeomChanged &&
           (prevPrX == prX) && (prevPrY == prY));

      if (hidePupil) {
        int16_t eraseRy = (prevEyeRy > 0) ? prevEyeRy : eyeRyNow;
        if (eyeRyNow < prevEyeRy) eraseRy = eyeRyNow;
        fillEllipseClippedToEye(prevPCx, prevPCy, prevPrX, prevPrY, eye, eraseRy, white_);
      } else if (pupilOnlyMove) {
        // Gaze-only: white only where old blue shows but new pupil won't cover, then new blue.
        applyPupilMoveDeltaClipped(prevPCx, prevPCy, prevPrX, prevPrY, pcx, pcy, prX, prY, eye, eyeRyNow);
      } else if (pupilGeomChanged) {
        int16_t eraseRy = (prevEyeRy > 0) ? prevEyeRy : eyeRyNow;
        if (eyeRyNow < prevEyeRy) eraseRy = eyeRyNow;
        fillEllipseClippedToEye(prevPCx, prevPCy, oldPrX, oldPrY, eye, eraseRy, white_);
        if (eyeRyNow > 1) fillEllipseClippedToEye(pcx, pcy, prX, prY, eye, eyeRyNow, blue_);
      } else if (opening) {
        if (eyeRyNow > 1) fillEllipseClippedToEye(pcx, pcy, prX, prY, eye, eyeRyNow, blue_);
      } else if (!hidePupil && hidePupilChanged) {
        if (eyeRyNow > 1) fillEllipseClippedToEye(pcx, pcy, prX, prY, eye, eyeRyNow, blue_);
      }
    }

    prevEyeRy = eyeRyNow;
    prevClosed = closedIs;
    prevPCx = pcx;
    prevPCy = pcy;
    prevPrX = prX;
    prevPrY = prY;
  };

  drawOne(L, -1, eyeRyL, prevEyeRyL_, prevClosedL_, prevPupilCxL_, prevPupilCyL_, prevPupilRxL_, prevPupilRyL_);
  drawOne(R, +1, eyeRyR, prevEyeRyR_, prevClosedR_, prevPupilCxR_, prevPupilCyR_, prevPupilRxR_, prevPupilRyR_);

  prevPupilRx_ = prX;
  prevPupilRy_ = prY;
  prevHidePupil_ = hidePupil;
  hasPrev_ = true;

  // 嘴巴：只有变化才重画（且不清背景，避免闪）
  drawMouth(frame, false);
}

