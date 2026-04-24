#include "include/globals.h"
#include "include/storage/image_store.h"
#include "include/core/config_store.h"
#include <vector>
#include <algorithm>

static constexpr size_t kImageBytes = 240u * 240u * 2u; // RGB565

static std::vector<int> g_imageSlots;

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
  g_imageSlots.clear();

  File root = LittleFS.open("/");
  if (root) {
    while (true) {
      File f = root.openNextFile();
      if (!f) break;
      String name = String(f.name());
      const size_t sz = f.size();
      f.close();

      // Some cores return names without a leading '/'.
      if (!name.startsWith("/")) name = "/" + name;
      if (!name.startsWith("/img") || !name.endsWith(".bin")) continue;
      if (sz != kImageBytes) {
        LittleFS.remove(name);
        continue;
      }
      const String numStr = name.substring(4, name.length() - 4);
      const int slot = numStr.toInt();
      if (slot < 0 || slot > 250) continue;
      g_imageSlots.push_back(slot);
    }
    root.close();
  }

  // sort + unique
  std::sort(g_imageSlots.begin(), g_imageSlots.end());
  g_imageSlots.erase(std::unique(g_imageSlots.begin(), g_imageSlots.end()), g_imageSlots.end());

  imageCount = (int)g_imageSlots.size();
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
  if (g_imageSlots.empty()) return;

  int curPos = -1;
  for (int i = 0; i < (int)g_imageSlots.size(); i++) {
    if (g_imageSlots[i] == currentImageIndex) { curPos = i; break; }
  }
  if (curPos < 0) curPos = 0;
  const int nextPos = (curPos + 1) % (int)g_imageSlots.size();
  currentImageIndex = g_imageSlots[nextPos];
  if (displayMode == 0) displayImageWithTransition(currentImageIndex);
  lastImageSwitch = millis();
  saveConfig();
}

bool image_has_slot(int slot) {
  if (slot < 0) return false;
  return LittleFS.exists("/img" + String(slot) + ".bin");
}

int image_next_free_slot() {
  // Find the smallest non-negative slot id not used.
  // Slot is uint8 in BLE payload, keep <= 250.
  for (int s = 0; s <= 250; s++) {
    if (!image_has_slot(s)) return s;
  }
  return -1;
}
