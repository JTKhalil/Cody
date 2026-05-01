#pragma once

#include "feature_flags.h"

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <LittleFS.h>
#include <time.h>
#include <sys/time.h>
#include <ArduinoJson.h>
#include <U8g2_for_Adafruit_GFX.h>

#if CODY_ENABLE_WIFI_DEBUG
  #include <WebServer.h>
  #include <WiFiManager.h>
  #include <HTTPUpdate.h>
  #include <WiFiClientSecure.h>
  #include <esp_wifi.h>
#endif

// 工程公共版本号
#include "version.h"

// 引脚宏在多个 .cpp 中都会用到，避免只在 .ino 可见
#ifndef TFT_CS
#define TFT_CS 4
#endif
#ifndef TFT_DC
#define TFT_DC 1
#endif
#ifndef TFT_RST
#define TFT_RST 2
#endif
#ifndef TFT_BLK
#define TFT_BLK 3
#endif

// 与 .ino 中的宏保持一致
#ifndef MAX_IMAGES
#define MAX_IMAGES 3
#endif

#ifndef DEFAULT_INTERVAL
#define DEFAULT_INTERVAL 10
#endif

#ifndef MAX_NOTES
#define MAX_NOTES 50
#endif

extern Adafruit_ST7789 tft;
extern U8G2_FOR_ADAFRUIT_GFX u8g2;

#if CODY_ENABLE_WIFI_DEBUG
extern WebServer server;
#endif

extern int lastMinute;
extern int displayMode;
/** 表情模式风格：0=Pupu（差分渲染 bubu），1=Coco（经典 actionStep） */
extern int exprGroup;
/** 设置页是否在前台；供 HTTP 等判断是否在设置内延迟刷新主界面 */
extern bool settingsActive;

extern int imageCount;
extern int currentImageIndex;
extern unsigned long lastImageSwitch;
extern int switchInterval;
extern bool slideshowEnabled;
extern bool slideshowPaused;

extern int pinnedNoteIndex;
extern bool noteSlideshowEnabled;
extern int noteSwitchInterval;
extern int currentNoteDisplayIndex;
extern unsigned long lastNoteSwitch;

// 时间是否已通过 BLE/串口校准（sync_time）
extern volatile bool g_timeCalibrated;

extern bool isUploading;
extern int uploadY;
extern int uploadingSlot;
extern File uploadFile;
extern size_t totalWritten;

extern String serialBuffer;

// 背光亮度（0-255，逻辑值；实际 PWM 可能做非线性映射）
extern int backlightValue;

#if CODY_ENABLE_WIFI_DEBUG
// WiFi 回退保护与状态变量
extern bool isTryingNewWifi;
extern String targetSSID;
extern String fallbackSSID;
extern String fallbackPSK;
extern unsigned long wifiTryStart;
#endif

