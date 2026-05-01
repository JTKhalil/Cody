#pragma once

#include <Arduino.h>

// 常用动态表情库（全自动循环，单表情 >=6s），输出每帧的“目标参数”。

enum class ExprStateId : uint8_t {
  SMILE = 0,
  WINK = 1,
  LOOK_LR = 2,
  LOOK_UD = 3,
  SAD = 4,
  ANGRY = 5,
  COUNT = 6,
};

enum class MouthId : uint8_t {
  NEUTRAL = 0,
  SMILE = 1,
  FROWN = 2,
  ANGRY = 3,
  SMIRK = 4,
  WAVY = 5,
  SLEEP_O = 6, // 睡颜：小椭圆 O 型嘴
  SMILE_INVERTED = 7, // 哭泣：向上凸平滑弧嘴（二次贝塞尔）
};

enum class BrowId : uint8_t {
  NEUTRAL = 0,
  SMILE = 1,
  WINK = 2,
  SAD = 3,
  ANGRY = 4,
  CURIOUS = 5,
};

struct ExprFrame {
  // eyes
  float gazeX;  // [-1,1]
  float gazeY;  // [-1,1]
  float openL;  // [0,1]
  float openR;  // [0,1]
  float pupil;  // [0,1] (typically fixed)
  float crossEyeT; // [0,1] 1=斗鸡眼基线偏置开启；0=关闭（用于某些轨迹更可控）
  bool hidePupil = false; // true：只画眼白，不画蓝瞳孔（如睡觉眯眼）

  // face lines
  MouthId mouth;
  BrowId brow;
  // 嘴中心相对默认 x=120 的像素偏移（唱歌等：整体略偏一侧）
  int16_t mouthCxOfs = 0;
  // SLEEP_O：0..1 小 o ↔ 大 O（仅睡觉表情使用）
  float sleepMouthO = 0.0f;
};

class ExpressionLibrary {
 public:
  void enter(uint32_t seed);
  void tick(unsigned long nowMs, ExprFrame& out);

 private:
  bool inited_ = false;
  uint32_t seed_ = 0;

  ExprStateId cur_ = ExprStateId::SMILE;
  ExprStateId next_ = ExprStateId::WINK;
  unsigned long stateStart_ = 0;
  unsigned long holdMs_ = 6000;

  bool inTrans_ = false;
  unsigned long transStart_ = 0;
  unsigned long transMs_ = 600;
  ExprFrame from_{};
  ExprFrame to_{};

  // shared blink event (subtle)
  bool blinking_ = false;
  unsigned long blinkStart_ = 0;
  unsigned long blinkDur_ = 180;
  unsigned long nextBlinkAt_ = 0;

  ExprFrame baseForState(ExprStateId id) const;
  void applyLoop(ExprStateId id, unsigned long nowMs, ExprFrame& io) const;
  void scheduleNext(unsigned long nowMs);
  void startTransition(unsigned long nowMs);
};

