#include "include/globals.h"
#include "include/storage/image_store.h"
#include "include/core/config_store.h"

static void setBacklight(int v) {
  v = constrain(v, 0, 255);
  int pwm = (v * v) / 255; // 与其它位置一致的感知亮度映射
  analogWrite(TFT_BLK, pwm);
}

static void fadeBacklight(int fromV, int toV, int totalMs) {
  fromV = constrain(fromV, 0, 255);
  toV = constrain(toV, 0, 255);
  const int steps = 12;
  for (int i = 0; i <= steps; i++) {
    int v = fromV + (toV - fromV) * i / steps;
    setBacklight(v);
    delay(totalMs / steps);
    yield();
  }
}

static void displayImageWithTransition(int slot) {
  // 更自然的轮播过渡：背光淡出 -> 切图 -> 淡入
  int target = constrain(backlightValue, 0, 255);
  fadeBacklight(target, max(0, target / 10), 160); // 淡出到 10%
  displayImageFromFile(slot);
  fadeBacklight(max(0, target / 10), target, 220); // 淡入
}

void scanImages() {
  imageCount = 0;
  for (int i = 0; i < MAX_IMAGES; i++) {
    String path = "/img" + String(i) + ".bin";
    if (LittleFS.exists(path)) {
      File f = LittleFS.open(path, "r");
      size_t sz = f.size();
      f.close();
      if (sz == 115200) imageCount++;
      else LittleFS.remove(path);
    }
  }
}

void displayImageFromFile(int slot) {
  String path = "/img" + String(slot) + ".bin";
  tft.fillScreen(ST77XX_BLACK);
  if (!LittleFS.exists(path)) {
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setForegroundColor(ST77XX_WHITE);
    int tw = u8g2.getUTF8Width("该槽位无图片");
    u8g2.setCursor((240 - tw) / 2, 136);
    u8g2.print("该槽位无图片");
    return;
  }
  File f = LittleFS.open(path, "r");
  if (!f) return;
  uint16_t lineBuffer[240];
  for (int y = 0; y < 240 && f.available() >= 480; y++) {
    f.read((uint8_t*)lineBuffer, 480);
    tft.drawRGBBitmap(0, y, lineBuffer, 240, 1);
  }
  f.close();
}

void loadSavedImage() {
  scanImages();
  if (imageCount > 0) {
    if (!LittleFS.exists("/img" + String(currentImageIndex) + ".bin")) nextImage();
    else displayImageFromFile(currentImageIndex);
  } else {
    tft.fillScreen(ST77XX_BLACK);
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setForegroundColor(ST77XX_WHITE);
    int tw = u8g2.getUTF8Width("图库空空如也");
    u8g2.setCursor((240 - tw) / 2, 136);
    u8g2.print("图库空空如也");
  }
}

void nextImage() {
  if (imageCount <= 1) return;
  int startIndex = currentImageIndex;
  do {
    currentImageIndex = (currentImageIndex + 1) % MAX_IMAGES;
    if (LittleFS.exists("/img" + String(currentImageIndex) + ".bin")) {
      if (displayMode == 0) displayImageWithTransition(currentImageIndex);
      lastImageSwitch = millis();
      saveConfig();
      break;
    }
  } while (currentImageIndex != startIndex);
}
