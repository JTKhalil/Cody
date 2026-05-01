#pragma once

#include "include/render/expression_pose.h"
#include "include/render/expression_library.h"

class ExpressionRenderer {
 public:
  void enter();
  void tick(const ExprFrame& frame);
  // 外部用背景色填了 ZZZ 等区域后调用：避免差分画眼时中间留白（右眼常见）
  void invalidateEyesAfterExternalClear();
  // 睡醒等场景：先铺掉嘴部区域，避免 O 圈黑色残留
  void clearMouthBoxToBg();

 private:
  bool inited_ = false;
  uint16_t bg_ = 0;     // background orange RGB565
  uint16_t white_ = 0;  // eye white RGB565
  uint16_t blue_ = 0;   // pupil blue RGB565

  // Previous frame state for differential updates (avoid full clear flicker)
  bool hasPrev_ = false;
  int16_t prevEyeRyL_ = -1;
  int16_t prevEyeRyR_ = -1;
  bool prevClosedL_ = false;
  bool prevClosedR_ = false;
  int16_t prevPupilCxL_ = 0, prevPupilCyL_ = 0;
  int16_t prevPupilCxR_ = 0, prevPupilCyR_ = 0;
  int16_t prevPupilRx_ = 0, prevPupilRy_ = 0;
  // Short-circuit for frames that don't change pixels
  int16_t prevPupilRxL_ = 0, prevPupilRyL_ = 0;
  int16_t prevPupilRxR_ = 0, prevPupilRyR_ = 0;
  bool prevHidePupil_ = false;

  void initColoursOnce();
  static BBox eyeMaxBBox(const EyeGeom& eye, int margin);
  void restoreBox(const BBox& b);

  // Filled ellipse by horizontal scanlines.
  void fillEllipse(int16_t cx, int16_t cy, int16_t rx, int16_t ry, uint16_t col);

  // Fill ellipse but clip to current eye opening (eye ellipse with ry = eyeRy).
  void fillEllipseClippedToEye(int16_t cx, int16_t cy, int16_t rx, int16_t ry,
                               const EyeGeom& eye, int16_t eyeRy, uint16_t col);

  // Pupil move-only: minimal repaint — white where old blue remains but new blue won't cover, then blue on new pupil.
  void applyPupilMoveDeltaClipped(int16_t cx0, int16_t cy0, int16_t rx0, int16_t ry0,
                                   int16_t cx1, int16_t cy1, int16_t rx1, int16_t ry1,
                                   const EyeGeom& eye, int16_t eyeRy);

  void drawClosedEyeLine(const EyeGeom& eye);

  // Differential update for eye white: update only changed edge segments between oldRy and newRy.
  void diffEyeWhite(const EyeGeom& eye, int16_t oldRy, int16_t newRy);

  // Mouth / brows（需盖住 SLEEP_O 最大椭圆 + 线宽；原先 h=45 底边 < 大 O 下缘导致残影）
  BBox mouthBox_ {52, 140, 136, 62};
  bool hasFacePrev_ = false;
  MouthId prevMouth_ = MouthId::NEUTRAL;
  int16_t prevMouthCxOfs_ = -32768;
  int16_t prevSleepOrxDraw_ = -32768;
  int16_t prevSleepOryDraw_ = -32768;

  void drawMouth(const ExprFrame& frame, bool force);
};

