#include "include/globals.h"
#include "include/net/http_handlers.h"
#include "include/util_hex.h"
#include "include/core/config_store.h"
#include "include/storage/image_store.h"
#include "include/render/display_render.h"
#include "include/render/handdraw.h"

#if !CODY_ENABLE_WIFI_DEBUG
void handleBrightness() {}
void handleFSSpace() {}
void handleImageInfo() {}
void handleGetImage() {}
void handleImageDelete() {}
void handleFormatFS() {}
void handleUploadStart() {}
void handleUploadChunk() {}
void handleUploadFinish() {}
void handleSetCurrent() {}
void handleSlideshowConfig() {}
void handleSetSlideshow() {}
void handleSetInterval() {}
void handleGetMode() {}
void handleSetMode() {}

#else
void handleBrightness() {
  if (server.hasArg("v")) {
    int v = constrain(server.arg("v").toInt(), 0, 255);
    int pwm = (v * v) / 255;
    backlightValue = v;
    analogWrite(TFT_BLK, pwm);
  }
  server.send(200, "text/plain", "OK");
}

void handleFSSpace() {
  size_t t = LittleFS.totalBytes(), u = LittleFS.usedBytes();
  server.send(200, "application/json",
              "{\"total\":" + String(t / 1024) + ",\"used\":" + String(u / 1024) +
                  ",\"free\":" + String((t - u) / 1024) + "}");
}

void handleImageInfo() {
  scanImages();
  size_t t = LittleFS.totalBytes(), u = LittleFS.usedBytes();
  StaticJsonDocument<512> doc;
  JsonArray arr = doc.createNestedArray("slots");
  for (int i = 0; i < MAX_IMAGES; i++) arr.add(LittleFS.exists("/img" + String(i) + ".bin"));
  doc["current"] = currentImageIndex;
  doc["count"] = imageCount;
  doc["fs_total"] = t / 1024;
  doc["fs_used"] = u / 1024;
  doc["fs_free"] = (t - u) / 1024;
  String r;
  serializeJson(doc, r);
  server.send(200, "application/json", r);
}

void handleGetImage() {
  if (!server.hasArg("slot")) {
    server.send(400, "text/plain", "Missing");
    return;
  }
  String path = "/img" + String(server.arg("slot").toInt()) + ".bin";
  if (!LittleFS.exists(path)) {
    server.send(404, "text/plain", "Not Found");
    return;
  }
  File f = LittleFS.open(path, "r");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

void handleImageDelete() {
  if (isUploading) {
    server.send(409, "application/json", "{\"status\":\"error\"}");
    return;
  }
  int s = server.hasArg("slot") ? server.arg("slot").toInt() : 0;
  LittleFS.remove("/img" + String(s) + ".bin");
  scanImages();
  if (currentImageIndex == s && imageCount > 0) nextImage();
  saveConfig();

  if (displayMode == 0) {
    if (imageCount == 0) {
      tft.fillScreen(ST77XX_BLACK);
      u8g2.setFont(u8g2_font_wqy16_t_gb2312);
      u8g2.setForegroundColor(ST77XX_WHITE);
      int tw = u8g2.getUTF8Width("图库空空如也");
      u8g2.setCursor((240 - tw) / 2, 136);
      u8g2.print("图库空空如也");
    } else {
      displayImageFromFile(currentImageIndex);
    }
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleFormatFS() {
  resetUserFilesystemToDefaults();
  if (displayMode == 0) tft.fillScreen(0);
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleUploadStart() {
  if (isUploading) {
    server.send(409, "application/json", "{\"status\":\"error\"}");
    return;
  }
  int s = server.hasArg("slot") ? server.arg("slot").toInt() : 0;
  if (s < 0 || s >= MAX_IMAGES) s = 0;
  uploadingSlot = s;

  size_t fS = LittleFS.totalBytes() - LittleFS.usedBytes();
  if (fS < 120000) {
    if (LittleFS.exists("/img" + String(s) + ".bin")) {
      LittleFS.remove("/img" + String(s) + ".bin");
      fS = LittleFS.totalBytes() - LittleFS.usedBytes();
    }
    if (fS < 120000) {
      server.send(500, "application/json", "{\"status\":\"error\"}");
      return;
    }
  }

  LittleFS.remove("/tmp_upload.bin");
  uploadFile = LittleFS.open("/tmp_upload.bin", "w");
  if (!uploadFile) {
    server.send(500, "application/json", "{\"status\":\"error\"}");
    return;
  }

  isUploading = true;
  slideshowPaused = true;
  uploadY = 0;
  totalWritten = 0;
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleUploadChunk() {
  if (!isUploading) {
    server.send(400, "application/json", "{\"status\":\"error\"}");
    return;
  }
  int y = server.arg("y").toInt();
  int h = server.arg("h").toInt();
  (void)y;

  String b = server.arg("plain");
  const char* hex = b.c_str();
  for (int row = 0; row < h; row++) {
    uint16_t lb[240];
    for (int x = 0; x < 240; x++) {
      int i = (row * 240 + x) * 4;
      lb[x] = (charToHex(hex[i]) << 12) | (charToHex(hex[i + 1]) << 8) |
              (charToHex(hex[i + 2]) << 4) | charToHex(hex[i + 3]);
    }
    size_t w = uploadFile.write((uint8_t*)lb, 480);
    if (w != 480) {
      uploadFile.close();
      LittleFS.remove("/tmp_upload.bin");
      isUploading = false;
      slideshowPaused = false;
      server.send(500, "application/json", "{\"status\":\"error\"}");
      return;
    }
    totalWritten += w;
  }
  uploadFile.flush();
  uploadY += h;
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleUploadFinish() {
  if (!isUploading) {
    server.send(400, "application/json", "{\"status\":\"error\"}");
    return;
  }
  uploadFile.close();
  delay(200);

  if (totalWritten != 115200) {
    LittleFS.remove("/tmp_upload.bin");
    isUploading = false;
    slideshowPaused = false;
    server.send(400, "application/json", "{\"status\":\"error\"}");
    return;
  }

  String tP = "/img" + String(uploadingSlot) + ".bin";
  LittleFS.remove(tP);
  if (!LittleFS.rename("/tmp_upload.bin", tP)) {
    isUploading = false;
    slideshowPaused = false;
    server.send(500, "application/json", "{\"status\":\"error\"}");
    return;
  }

  isUploading = false;
  slideshowPaused = false;
  totalWritten = 0;
  scanImages();
  currentImageIndex = uploadingSlot;
  saveConfig();

  if (displayMode == 0) {
    displayImageFromFile(currentImageIndex);
    lastImageSwitch = millis();
  }
  printFSSpace();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleSetCurrent() {
  if (server.hasArg("slot")) {
    int s = server.arg("slot").toInt();
    if (LittleFS.exists("/img" + String(s) + ".bin")) {
      currentImageIndex = s;
      saveConfig();
      if (displayMode == 0) {
        displayImageFromFile(currentImageIndex);
        lastImageSwitch = millis();
      }
    }
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleSlideshowConfig() {
  server.send(200, "application/json",
              "{\"enabled\":" + String(slideshowEnabled ? "true" : "false") +
                  ",\"interval\":" + String(switchInterval) + "}");
}

void handleSetSlideshow() {
  if (server.hasArg("enabled")) {
    slideshowEnabled = (server.arg("enabled") == "true");
    saveConfig();
    if (slideshowEnabled) lastImageSwitch = millis();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
    return;
  }
  server.send(400, "application/json", "{\"status\":\"error\"}");
}

void handleSetInterval() {
  if (server.hasArg("value")) {
    switchInterval = constrain(server.arg("value").toInt(), 3, 60);
    saveConfig();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
    return;
  }
  server.send(400, "application/json", "{\"status\":\"error\"}");
}

void handleGetMode() {
  server.send(200, "application/json", "{\"mode\":" + String(displayMode) + "}");
}

void handleSetMode() {
  if (server.hasArg("mode")) {
    int nM = server.arg("mode").toInt();
    if (nM >= 0 && nM <= 4) {
      const bool leavingDraw = (displayMode == 4 && nM != 4);
      if (leavingDraw && !handdraw_ble_idle_for_ms(150)) {
        server.send(200, "application/json", "{\"status\":\"error\",\"msg\":\"handdraw_transfer_busy\"}");
        return;
      }
      if (leavingDraw) {
        handdraw_flush_persist_now();
      }
      displayMode = nM;
      saveConfig();
      // 在设置页内仅后台切换模式并落盘，不抢屏；退出设置时 refreshDisplayByMode() 会显示新模式
      if (!settingsActive) {
        refreshDisplayByMode();
      }
      server.send(200, "application/json", "{\"status\":\"ok\"}");
      return;
    }
  }
  server.send(400, "application/json", "{\"status\":\"error\"}");
}

#endif

