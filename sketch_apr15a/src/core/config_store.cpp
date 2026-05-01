#include "include/globals.h"
#include "include/core/config_store.h"
#include "include/render/handdraw.h"

void printFSSpace() {
  size_t total = LittleFS.totalBytes();
  size_t used = LittleFS.usedBytes();
  size_t free = total - used;
  Serial.printf("FS: %dKB total, %dKB used, %dKB free\n", total / 1024, used / 1024, free / 1024);
}

void cleanupTempUploads() {
  LittleFS.remove("/tmp_upload.bin");
}

void resetUserFilesystemToDefaults() {
  handdraw_release_buffer();
  LittleFS.format();
  currentImageIndex = 0;
  imageCount = 0;
  pinnedNoteIndex = -1;
  currentNoteDisplayIndex = 0;
  noteSlideshowEnabled = false;
  noteSwitchInterval = 10;
  switchInterval = 10;
  slideshowEnabled = true;
  lastImageSwitch = 0;
  lastNoteSwitch = 0;
  exprGroup = 0;
  saveConfig();
}

void saveConfig() {
  File f = LittleFS.open("/config.txt", "w");
  if (!f) return;

  f.print(currentImageIndex); f.print("\n");
  f.print(switchInterval); f.print("\n");
  f.print(slideshowEnabled ? "1" : "0"); f.print("\n");
  f.print(displayMode); f.print("\n");
  f.print(pinnedNoteIndex); f.print("\n");
  f.print(noteSlideshowEnabled ? "1" : "0"); f.print("\n");
  f.print(noteSwitchInterval); f.print("\n");
  f.print(exprGroup); f.print("\n");
  f.close();
}

void loadConfig() {
  if (LittleFS.exists("/config.txt")) {
    File f = LittleFS.open("/config.txt", "r");
    if (f) {
      if (f.available()) currentImageIndex = f.parseInt();
      if (f.available()) switchInterval = f.parseInt();
      if (f.available()) slideshowEnabled = f.parseInt() == 1;
      if (f.available()) displayMode = f.parseInt();
      if (f.available()) pinnedNoteIndex = f.parseInt(); else pinnedNoteIndex = -1;
      if (f.available()) noteSlideshowEnabled = f.parseInt() == 1; else noteSlideshowEnabled = false;
      if (f.available()) noteSwitchInterval = f.parseInt(); else noteSwitchInterval = 10;
      if (f.available()) exprGroup = f.parseInt(); else exprGroup = 0;
      f.close();
    }
  }

  if (exprGroup != 0 && exprGroup != 1) exprGroup = 0;

  switchInterval = constrain(switchInterval, 3, 60);
  noteSwitchInterval = constrain(noteSwitchInterval, 3, 60);
  if (currentImageIndex < 0) currentImageIndex = 0;
  // 0=图片 1=时钟 2=笔记 3=表情 4=手绘
  if (displayMode < 0 || displayMode > 4) displayMode = 0;
}

