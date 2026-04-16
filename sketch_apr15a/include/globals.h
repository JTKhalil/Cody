#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <LittleFS.h>
#include <time.h>
#include <sys/time.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <esp_wifi.h>

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
#define MAX_IMAGES 4
#endif

#ifndef DEFAULT_INTERVAL
#define DEFAULT_INTERVAL 10
#endif

#ifndef MAX_NOTES
#define MAX_NOTES 50
#endif

extern Adafruit_ST7789 tft;
extern U8G2_FOR_ADAFRUIT_GFX u8g2;
extern WebServer server;

extern int lastMinute;
extern int displayMode;

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

extern bool isUploading;
extern int uploadY;
extern int uploadingSlot;
extern File uploadFile;
extern size_t totalWritten;

extern String serialBuffer;

// WiFi 回退保护与状态变量
extern bool isTryingNewWifi;
extern String targetSSID;
extern String fallbackSSID;
extern String fallbackPSK;
extern unsigned long wifiTryStart;

