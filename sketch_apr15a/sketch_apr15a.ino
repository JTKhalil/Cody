#include "include/globals.h"
#include "include/ui/web_ui.h"
#include "include/core/config_store.h"
#include "include/storage/image_store.h"
#include "include/render/display_render.h"
#include "include/render/expression_mode.h"
#include "include/net/http_handlers.h"
#include "include/storage/note_store.h"
#include "include/net/ota_update.h"
#include "include/net/system_ops.h"
#include "include/protocol/serial_protocol.h"

// ── 1. 核心配置与引脚 ──────────────────────────────────────────
const char* URL_VERSION = "https://raw.githubusercontent.com/JTKhalil/claudeRobot/refs/heads/main/version.txt";
const char* URL_BIN = "https://raw.githubusercontent.com/JTKhalil/claudeRobot/refs/heads/main/firmware.bin";

#define TFT_CS 4
#define TFT_DC 1
#define TFT_RST 2
#define TFT_BLK 3

#define MAX_IMAGES 4
#define DEFAULT_INTERVAL 10
#define TARGET_SIZE 240
#define MAX_NOTES 50

// ── 按键（可按实际接线覆盖） ───────────────────────────────────
// ESP32-C3 常见：BOOT=GPIO9（板载 BOOT 键）
#ifndef BTN_BOOT_PIN
#define BTN_BOOT_PIN 9
#endif

static const unsigned long BTN_DEBOUNCE_MS = 35;

// BOOT 短按/长按阈值
static const unsigned long BOOT_SHORT_MAX_MS = 600;
static const unsigned long BOOT_LONG_INFO_MS = 1800;
static const unsigned long BOOT_LONG_FORMAT_MS = 8000; // 信息页内长按触发格式化（防误触，时长加长）
static const unsigned long BOOT_HOLD_SHOW_PROGRESS_MS = 2000;

static bool infoPageActive = false;
static int displayModeBeforeInfo = 0;

// 去抖后的稳定电平与原始采样
static bool bootRawPrev = true;
static bool bootStableLevel = true;
static unsigned long bootRawChangeAt = 0;

static bool bootIsHolding = false;
static unsigned long bootHoldStart = 0;
static bool bootProgressShowing = false;
static unsigned long bootLastProgressDraw = 0;
static bool bootLongActionFired = false;
static int bootLastSecondShown = -1;

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2; 
WebServer server(80);

int lastMinute = -1;
int displayMode = 0;

int imageCount = 0;
int currentImageIndex = 0;
unsigned long lastImageSwitch = 0;
int switchInterval = DEFAULT_INTERVAL;
bool slideshowEnabled = true;
bool slideshowPaused = false;

int pinnedNoteIndex = -1; 
bool noteSlideshowEnabled = false;
int noteSwitchInterval = 10;
int currentNoteDisplayIndex = 0;
unsigned long lastNoteSwitch = 0;

bool isUploading = false;
int uploadY = 0;
int uploadingSlot = 0; 
File uploadFile;
size_t totalWritten = 0;

String serialBuffer = "";

// --- WiFi 回退保护与状态变量 ---
bool isTryingNewWifi = false;
String targetSSID = ""; // 新增：必须校验目标网络名称，防止幽灵连接状态
String fallbackSSID = "";
String fallbackPSK = "";
unsigned long wifiTryStart = 0;
// ---------------------------------
void handleNextImage() { nextImage(); server.send(200, "application/json", "{\"status\":\"ok\"}"); }
void handlePrevImage() { prevImage(); server.send(200, "application/json", "{\"status\":\"ok\"}"); }

// ── 5. 主程序 ─────────────────────────────────────────────────
void setup() {
  Serial.setRxBufferSize(4096); 
  Serial.setTxBufferSize(8192);
  Serial.begin(115200);
  serialBuffer.reserve(8192); 

  // 按键：低电平按下
  pinMode(BTN_BOOT_PIN, INPUT_PULLUP);
  
  pinMode(TFT_BLK, OUTPUT); analogWrite(TFT_BLK, 255);
  SPI.begin(8, -1, 10, TFT_CS);
  tft.init(240, 240); tft.setRotation(1); tft.fillScreen(ST77XX_BLACK);
  
  u8g2.begin(tft);
  u8g2.setFontMode(1); 
  u8g2.setFontDirection(0);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);

  if (!LittleFS.begin(true)) { LittleFS.format(); LittleFS.begin(true); }
  
  cleanupTempUploads(); loadConfig();
  
  WiFi.mode(WIFI_STA); 
  wifi_config_t conf;
  esp_wifi_get_config(WIFI_IF_STA, &conf);
  String lastSSID = String((char*)conf.sta.ssid);

  drawBootSplash();

  bool serialDetected = false;
  unsigned long bootWait = millis();
  while (millis() - bootWait < 2500) {
    if (Serial.available()) {
      serialDetected = true;
      break;
    }
    delay(50);
  }

  if (serialDetected) {
    tft.fillScreen(ST77XX_BLACK);
    u8g2.setForegroundColor(ST77XX_GREEN);
    u8g2.setCursor(10, 50); u8g2.print("已连接至PC控制台");
    delay(1500);

    WiFi.begin();
    configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");

  } else {
    tft.fillScreen(ST77XX_BLACK);
    u8g2.setForegroundColor(ST77XX_WHITE);
    u8g2.setCursor(10, 30); u8g2.print("正在尝试连接网络...");

    if (lastSSID.length() > 0) {
      u8g2.setCursor(10, 60); u8g2.print("上次使用的网络:");
      tft.setTextColor(ST77XX_CYAN); tft.setTextSize(2); tft.setCursor(10, 70); tft.print(lastSSID);
    } else {
      u8g2.setCursor(10, 60); u8g2.print("暂无历史网络记录");
    }

    u8g2.setForegroundColor(ST77XX_ORANGE);
    u8g2.setCursor(10, 195); u8g2.print("如失败将自动开启配网热点");
    
    WiFi.begin(); 
    int countdown = 15;
    bool connected = false;

    while (countdown > 0) {
      if (WiFi.status() == WL_CONNECTED) {
        connected = true;
        break;
      }
      
      tft.fillRect(10, 130, 220, 30, ST77XX_BLACK);
      u8g2.setForegroundColor(ST77XX_WHITE);
      u8g2.setCursor(10, 150);
      u8g2.print("倒计时: ");
      u8g2.setForegroundColor(ST77XX_RED);
      u8g2.print(countdown);
      u8g2.setForegroundColor(ST77XX_WHITE);
      u8g2.print(" 秒");

      for(int d = 0; d < 10; d++) {
          while (Serial.available()) {
            char c = Serial.read();
            if (c == '\n') {
              if (serialBuffer.length() > 0) { processSerialCommand(serialBuffer); serialBuffer = ""; }
            } else if (c != '\r') { serialBuffer += c; }
          }
          delay(100);
      }
      countdown--;
    }

    if (!connected) {
      WiFiManager wm;
      wm.setAPCallback(configModeCallback);
      wm.setConfigPortalTimeout(180); 
      
      if (!wm.startConfigPortal("Cody-Screen")) {
        tft.fillScreen(ST77XX_BLACK); 
        u8g2.setForegroundColor(ST77XX_RED); 
        u8g2.setCursor(10, 96); u8g2.print("配网超时, 正在重启...");
        delay(3000); 
        ESP.restart();
      }
    }

    configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");
    
    tft.fillScreen(ST77XX_BLACK);
    u8g2.setForegroundColor(ST77XX_GREEN); u8g2.setCursor(10, 36); u8g2.print("网络连接成功！");
    u8g2.setForegroundColor(ST77XX_WHITE); u8g2.setCursor(10, 71); u8g2.print("浏览器访问:"); 
    tft.setTextColor(ST77XX_CYAN); tft.setTextSize(2); tft.setCursor(10, 85); tft.print(WiFi.localIP().toString());
    u8g2.setForegroundColor(ST77XX_WHITE); u8g2.setCursor(10, 128); u8g2.print("可进入局域网控制台"); 

    
    for (int i = 10; i >= 1; i--) {
      tft.fillRect(0, 200, 240, 40, ST77XX_BLACK); 
      u8g2.setForegroundColor(ST77XX_WHITE); u8g2.setCursor(10, 226); u8g2.print("将在 "); 
      u8g2.setForegroundColor(ST77XX_YELLOW); u8g2.print(i); 
      u8g2.setForegroundColor(ST77XX_WHITE); u8g2.print(" 秒后启动...");
      delay(1000);
    }
  }
  
  scanImages(); refreshDisplayByMode(); lastImageSwitch = millis(); lastNoteSwitch = millis();
  
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/image_info", HTTP_GET, handleImageInfo);
  server.on("/get_image", HTTP_GET, handleGetImage);
  server.on("/delete_image", HTTP_GET, handleImageDelete);
  server.on("/format_fs", HTTP_GET, handleFormatFS);
  server.on("/fs_space", HTTP_GET, handleFSSpace);
  server.on("/upload_start", HTTP_GET, handleUploadStart);
  server.on("/upload_chunk", HTTP_POST, handleUploadChunk);
  server.on("/upload_finish", HTTP_GET, handleUploadFinish);
  server.on("/set_current", HTTP_GET, handleSetCurrent);
  
  server.on("/get_notes", HTTP_GET, handleGetNotes);
  server.on("/save_note", HTTP_GET, handleSaveNote);
  server.on("/delete_note", HTTP_GET, handleDeleteNote);
  server.on("/note_config", HTTP_GET, handleNoteConfig);
  server.on("/set_note_config", HTTP_GET, handleSetNoteConfig);

  server.on("/slideshow_config", HTTP_GET, handleSlideshowConfig);
  server.on("/slideshow", HTTP_GET, handleSetSlideshow);
  server.on("/interval", HTTP_GET, handleSetInterval);
  server.on("/next", HTTP_GET, handleNextImage);
  server.on("/prev", HTTP_GET, handlePrevImage);
  server.on("/bright", HTTP_GET, handleBrightness);
  server.on("/ota_info", HTTP_GET, handleOtaInfo);
  server.on("/do_update", HTTP_GET, handleDoUpdate);
  server.on("/reset_system", HTTP_GET, handleResetSystem);
  server.on("/get_mode", HTTP_GET, handleGetMode);
  server.on("/set_mode", HTTP_GET, handleSetMode);
  server.begin();
}

static void enterInfoPage() {
  infoPageActive = true;
  displayModeBeforeInfo = displayMode;
  drawSystemInfoPage();
}

static void exitInfoPage() {
  infoPageActive = false;
  refreshDisplayByMode();
}

static void toggleInfoPage() {
  if (infoPageActive) exitInfoPage();
  else enterInfoPage();
}

static void cycleDisplayMode() {
  displayMode = (displayMode + 1) % 4;
  saveConfig();
  refreshDisplayByMode();
}

static void doFactoryFormatAll() {
  // 全量格式化：清空 LittleFS（图片/笔记/配置），并重置内存态
  tft.fillScreen(ST77XX_BLACK);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setForegroundColor(ST77XX_ORANGE);
  u8g2.setCursor(10, 96);
  u8g2.print("正在格式化...");

  // 额外：清除 WiFi 配网信息（恢复出厂网络）
  {
    WiFi.disconnect(true, true);
    delay(200);
    WiFiManager wm;
    wm.resetSettings();
    delay(200);
  }

  LittleFS.format();
  delay(200);
  LittleFS.begin(true);

  currentImageIndex = 0;
  imageCount = 0;
  pinnedNoteIndex = -1;
  noteSlideshowEnabled = false;
  currentNoteDisplayIndex = 0;
  saveConfig();

  tft.fillScreen(ST77XX_BLACK);
  u8g2.setForegroundColor(ST77XX_GREEN);
  u8g2.setCursor(10, 96);
  u8g2.print("格式化完成，重启中");
  delay(800);
  ESP.restart();
}

static void handleButtons() {
  const unsigned long now = millis();

  // --- BOOT（短按：切换模式；长按：进入信息页；信息页长按：格式化） ---
  bool bootRaw = digitalRead(BTN_BOOT_PIN); // true=未按, false=按下
  if (bootRaw != bootRawPrev) {
    bootRawPrev = bootRaw;
    bootRawChangeAt = now;
  }
  if ((now - bootRawChangeAt) > BTN_DEBOUNCE_MS && bootStableLevel != bootRaw) {
    // 稳定电平变化（边沿事件）
    bootStableLevel = bootRaw;

    if (bootStableLevel == false) { // 按下
      bootIsHolding = true;
      bootHoldStart = now;
      bootProgressShowing = false;
      bootLongActionFired = false;
      bootLastSecondShown = -1;
    } else { // 松开
      if (!bootIsHolding) return;
      const unsigned long held = now - bootHoldStart;
      const bool fired = bootLongActionFired;

      bootIsHolding = false;
      bootProgressShowing = false;
      bootLongActionFired = false;
      bootLastSecondShown = -1;

      if (!fired && held <= BOOT_SHORT_MAX_MS) {
        // 短按：信息页返回 / 普通切换模式
        if (infoPageActive) exitInfoPage();
        else cycleDisplayMode();
      } else {
        // 松开取消：回到对应页面
        if (infoPageActive) drawSystemInfoPage();
        else refreshDisplayByMode();
      }
    }
  }

  // 长按过程（不等松开）：达到阈值立即执行动作
  if (bootIsHolding && !bootLongActionFired) {
    unsigned long held = now - bootHoldStart;

    // 2 秒后显示动态进度条
    if (held >= BOOT_HOLD_SHOW_PROGRESS_MS) {
      bootProgressShowing = true;
      int secondsHeld = static_cast<int>(held / 1000);
      if (secondsHeld != bootLastSecondShown) {
        if (infoPageActive) {
          drawHoldProgress("长按 BOOT 格式化", "保持按住", secondsHeld, static_cast<int>(BOOT_LONG_FORMAT_MS / 1000));
        } else {
          drawHoldProgress("长按 BOOT 查看信息", "保持按住", secondsHeld, static_cast<int>(BOOT_LONG_INFO_MS / 1000));
        }
        bootLastSecondShown = secondsHeld;
      }
    }

    // 信息页内：长按触发格式化
    if (infoPageActive) {
      if (held >= BOOT_LONG_FORMAT_MS) {
        bootLongActionFired = true;
        doFactoryFormatAll();
      }
    } else {
      // 非信息页：长按进入信息页
      if (held >= BOOT_LONG_INFO_MS) {
        bootLongActionFired = true;
        enterInfoPage();
      }
    }
  }
}

void loop() {
  server.handleClient();

  handleButtons();
  
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      if (serialBuffer.length() > 0) {
        processSerialCommand(serialBuffer);
        serialBuffer = ""; 
      }
    } else if (c != '\r') {
      serialBuffer += c;
    }
  }

  if (isTryingNewWifi) {
    unsigned long elapsed = millis() - wifiTryStart;
    if (elapsed > 3000 && WiFi.status() == WL_CONNECTED && WiFi.SSID() == targetSSID) {
      isTryingNewWifi = false; 
    } else if (elapsed > 15000) { 
      WiFi.disconnect();
      delay(500); 
      if (fallbackSSID.length() > 0) {
        WiFi.begin(fallbackSSID.c_str(), fallbackPSK.c_str());
      } else {
        WiFi.begin(); 
      }
      isTryingNewWifi = false;
    }
  }
  
  if (infoPageActive || bootIsHolding || bootProgressShowing) {
    // 信息页/长按重启时不跑模式刷新
  }
  else if (displayMode == 1) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo) && timeinfo.tm_min != lastMinute) { lastMinute = timeinfo.tm_min; drawClockFace(); }
  } 
  else if (displayMode == 0) {
    if (slideshowEnabled && !slideshowPaused && imageCount > 1) {
      if (millis() - lastImageSwitch > switchInterval * 1000) nextImage();
    }
  }
  else if (displayMode == 2) {
    if (pinnedNoteIndex == -1 && noteSlideshowEnabled) {
       StaticJsonDocument<4096> doc;
       if (LittleFS.exists("/notes.json")) {
           File f = LittleFS.open("/notes.json", "r"); deserializeJson(doc, f); f.close();
           int noteCount = doc.size();
           if (noteCount > 1) {
               if (millis() - lastNoteSwitch > noteSwitchInterval * 1000) {
                   currentNoteDisplayIndex = (currentNoteDisplayIndex + 1) % noteCount;
                   displayNoteOnScreen();
                   lastNoteSwitch = millis();
               }
           }
       }
    }
  }
  else if (displayMode == 3) {
    expressionModeTick();
  }
}