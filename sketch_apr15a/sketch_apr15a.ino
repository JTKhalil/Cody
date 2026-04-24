#include "include/globals.h"
#include "include/feature_flags.h"

#if CODY_ENABLE_WIFI_DEBUG
#include "include/ui/web_ui.h"
#include "include/net/http_handlers.h"
#include "include/storage/note_store.h"
#include "include/net/ota_update.h"
#include "include/net/system_ops.h"
#endif
#include "include/core/config_store.h"
#include "include/storage/image_store.h"
#include "include/render/display_render.h"
#include "include/render/handdraw.h"
#include "include/render/expression_mode.h"
#include "include/protocol/serial_protocol.h"
#include "include/ble/cody_ble.h"
#include "include/ble/ble_image.h"
#include "include/ble/ble_ota.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ── 1. 核心配置与引脚 ──────────────────────────────────────────
const char* URL_VERSION = "https://raw.githubusercontent.com/JTKhalil/claudeRobot/refs/heads/main/version.txt";
const char* URL_BIN = "https://raw.githubusercontent.com/JTKhalil/claudeRobot/refs/heads/main/firmware.bin";

#define TFT_CS 4
#define TFT_DC 1
#define TFT_RST 2
#define TFT_BLK 3

#define MAX_IMAGES 3
#define DEFAULT_INTERVAL 10
#define TARGET_SIZE 240
#define MAX_NOTES 50

// 开机 Logo 停留时长（毫秒）
#define BOOT_SPLASH_HOLD_MS 3000

// ── 按键（可按实际接线覆盖） ───────────────────────────────────
// ESP32-C3 常见：BOOT=GPIO9（板载 BOOT 键）
#ifndef BTN_BOOT_PIN
#define BTN_BOOT_PIN 9
#endif

// 左键（用于 BLE 配对确认）：默认与 BOOT 相同，若你的硬件左键另有 GPIO，可在编译时覆盖该宏
#ifndef BTN_LEFT_PIN
#define BTN_LEFT_PIN BTN_BOOT_PIN
#endif

static const unsigned long BTN_DEBOUNCE_MS = 35;

// BOOT 短按/长按阈值
static const unsigned long BOOT_SHORT_MAX_MS = 600;
static const unsigned long BOOT_LONG_SETTINGS_MS = 1200;
/** 长按进度 UI：按住满该时长后才开始从 0→1 填充（映射到 BOOT_LONG_SETTINGS_MS 之后的剩余段） */
static const unsigned long SETTINGS_LONG_PRESS_PROGRESS_DELAY_MS = 500;

// 去抖后的稳定电平与原始采样
static bool bootRawPrev = true;
static bool bootStableLevel = true;
static unsigned long bootRawChangeAt = 0;

static bool bootIsHolding = false;
static unsigned long bootHoldStart = 0;
static bool bootLongActionFired = false;

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2; 
#if CODY_ENABLE_WIFI_DEBUG
WebServer server(80);
#endif

int lastMinute = -1;
int displayMode = 0;
int backlightValue = 255;

int imageCount = 0;
int currentImageIndex = 0;
unsigned long lastImageSwitch = 0;
int switchInterval = DEFAULT_INTERVAL;
bool slideshowEnabled = true;
bool slideshowPaused = false;

volatile bool g_timeCalibrated = false;

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

#if CODY_ENABLE_WIFI_DEBUG
// --- WiFi 回退保护与状态变量 ---
bool isTryingNewWifi = false;
String targetSSID = ""; // 新增：必须校验目标网络名称，防止幽灵连接状态
String fallbackSSID = "";
String fallbackPSK = "";
unsigned long wifiTryStart = 0;
// ---------------------------------
#endif

// --- 开机 WiFi：后台只尝试一次，失败后停止 ---
static bool bootWifiAttemptActive = false;
static bool bootWifiAttemptDone = false;
static unsigned long bootWifiAttemptStart = 0;
static const unsigned long BOOT_WIFI_ONESHOT_TIMEOUT_MS = 15000;
// -------------------------------------------

// --- 设置页状态机 ---
enum SettingsPage {
  SET_PAGE_MENU = 0,
  SET_PAGE_BLE_INFO = 1,
  // 旧页面保留枚举值，但在 wxcody-ble 固件中不再提供入口
  SET_PAGE_SOFT_UPDATE = 2,
  SET_PAGE_ABOUT = 3,
};
bool settingsActive = false;
static SettingsPage settingsPage = SET_PAGE_MENU;
static int settingsSelected = 0;      // menu: 0..2（已移除 WiFi/Web 相关项）
static int settingsSubSelected = 0;   // sub pages: 0..1
// 软件更新信息缓存（进入软件更新页时拉取一次）
static bool settingsOtaFetched = false;
static String settingsLatestVer = "";
static String settingsNotes = "";
static bool settingsUpdateAvailable = false;
enum SettingsOtaState { OTA_IDLE = 0, OTA_CHECKING = 1, OTA_DONE = 2 };
static volatile SettingsOtaState settingsOtaState = OTA_IDLE;
static volatile bool settingsOtaCheckFinished = false;
static TaskHandle_t settingsOtaTaskHandle = nullptr;
static SemaphoreHandle_t settingsOtaMutex = nullptr;
// 电脑连接：开机动画内已显示则不再用底部条；主界面后首次收到串口仍可用底部条 3 秒
static unsigned long g_pcSerialToastEnd = 0;
static bool g_pcSerialToastWasShowing = false;
static bool g_pcSerialFirstRxSeen = false;
static bool g_pcSerialToastOverlayDrawn = false;
// 设置菜单「抹除全部」：长按进入进度页，松手取消；进度满 100% 后短暂停再执行
static bool settingsFormatHoldActive = false;
static unsigned long settingsFormatHoldStartMs = 0;
static bool settingsFormatHoldFullPhase = false;
static unsigned long settingsFormatHoldFullSinceMs = 0;
static const uint32_t SETTINGS_FORMAT_HOLD_MS = 8000;
static const uint32_t SETTINGS_FORMAT_PAUSE_AT_FULL_MS = 280;
/** 长按进度节流：为 (held-延迟)/SETTINGS_LONG_PRESS_THROTTLE_MS，松手或执行后 0xFFFF */
static uint16_t g_settingsLongPressThrottle = 0xFFFF;
/** 长按进度重绘最小间隔（ms 桶），增大可明显减轻闪屏 */
static const unsigned long SETTINGS_LONG_PRESS_THROTTLE_MS = 16;
// --------------------

// 软件更新页提示文案：避免 WiFi 已连接但 hint 为空时被画成「已是最新版本」；检查中始终显示「正在检查中...」
#if CODY_ENABLE_WIFI_DEBUG
static const char* settingsSoftUpdateHintForDraw() {
  if (WiFi.status() != WL_CONNECTED) {
    return "未联网，无法检查更新";
  }
  if (!settingsOtaCheckFinished) {
    return "正在检查中...";
  }
  if (!settingsOtaFetched) {
    return "检查失败";
  }
  return "";
}

static void settingsOtaCheckTask(void* pv) {
  (void)pv;
  vTaskDelay(pdMS_TO_TICKS(40));

  String latest, notes;
  bool ok = fetchRemoteVersionInfo(latest, notes);

  if (settingsOtaMutex) xSemaphoreTake(settingsOtaMutex, portMAX_DELAY);
  settingsOtaFetched = ok;
  if (ok) {
    settingsLatestVer = latest;
    settingsNotes = notes;
    settingsUpdateAvailable = (compareVersionParts(settingsLatestVer.c_str(), CURRENT_VERSION) > 0);
  } else {
    settingsLatestVer = "";
    settingsNotes = "";
    settingsUpdateAvailable = false;
  }
  settingsOtaCheckFinished = true;
  settingsOtaState = OTA_DONE;
  if (settingsOtaMutex) xSemaphoreGive(settingsOtaMutex);

  settingsOtaTaskHandle = nullptr;
  vTaskDelete(nullptr);
}
#else
static const char* settingsSoftUpdateHintForDraw() {
  return "WiFi/Web 调试已关闭";
}
#endif

// ── 5. 主程序 ─────────────────────────────────────────────────
void setup() {
  Serial.setRxBufferSize(4096); 
  Serial.setTxBufferSize(8192);
  Serial.begin(115200);
  serialBuffer.reserve(8192); 

  // 按键：低电平按下
  pinMode(BTN_BOOT_PIN, INPUT_PULLUP);
  if (BTN_LEFT_PIN != BTN_BOOT_PIN) {
    pinMode(BTN_LEFT_PIN, INPUT_PULLUP);
  }
  
  pinMode(TFT_BLK, OUTPUT);
  backlightValue = 255;
  // 先关背光再上电初始化 TFT，避免随机显存/未初始化阶段被点亮造成“花屏”
  analogWrite(TFT_BLK, 0);
  SPI.begin(8, -1, 10, TFT_CS);
  tft.init(240, 240);
  // ST7789：SLPOUT/DISPON 后部分模组需更长时间 GRAM 才稳定；库内默认延时偏短，易在边角出现一瞬花屏
  delay(120);
  tft.setRotation(1);
  delay(50);
  tft.fillScreen(ST77XX_BLACK);
  tft.fillScreen(ST77XX_BLACK);
  // 240x240 裁切屏偶发右下角残留，局部再清一次（成本可忽略）
  tft.fillRect(160, 160, 80, 80, ST77XX_BLACK);
  delay(40);

  {
    const int pwm = (backlightValue * backlightValue) / 255;
    analogWrite(TFT_BLK, pwm);
  }
  
  u8g2.begin(tft);
  u8g2.setFontMode(1); 
  u8g2.setFontDirection(0);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);

  // 必须先显示 Logo：LittleFS 挂载/格式化或 WiFi/NVS 异常时可能长时间阻塞或复位，
  // 若 Logo 放在其后会导致“黑屏、像无法开机”。
  drawBootSplash(false);
  for (unsigned long splashUntil = millis() + BOOT_SPLASH_HOLD_MS; millis() < splashUntil;) {
    if (!g_pcSerialFirstRxSeen && Serial.available() > 0) {
      g_pcSerialFirstRxSeen = true;
      drawBootSplash(true);
    }
    delay(10);
  }

  if (!LittleFS.begin(true)) { LittleFS.format(); LittleFS.begin(true); }

  // 无 WiFi/NTP 也要能显示本地时间：设置时区（中国 UTC+8）
  setenv("TZ", "CST-8", 1);
  tzset();
  
  cleanupTempUploads(); loadConfig();
  
#if CODY_ENABLE_WIFI_DEBUG
  WiFi.mode(WIFI_STA); 
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false); // 失败后不自动反复重连
  wifi_config_t conf;
  esp_wifi_get_config(WIFI_IF_STA, &conf);
  (void)conf;
#endif

  scanImages();
  refreshDisplayByMode();
  lastImageSwitch = millis();
  lastNoteSwitch = millis();

  // 与是否连接 PC 串口无关：统一后台尝试联网一次（HTML 串口连上时不再全屏阻塞/不因此改变启动流程）
  #if CODY_ENABLE_WIFI_DEBUG
  WiFi.begin();
  configTime(8 * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");
  bootWifiAttemptActive = true;
  bootWifiAttemptDone = false;
  bootWifiAttemptStart = millis();
  #else
  bootWifiAttemptActive = false;
  bootWifiAttemptDone = true;
  #endif
  
#if CODY_ENABLE_WIFI_DEBUG
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
  server.on("/bright", HTTP_GET, handleBrightness);
  server.on("/ota_info", HTTP_GET, handleOtaInfo);
  server.on("/do_update", HTTP_GET, handleDoUpdate);
  server.on("/reset_system", HTTP_GET, handleResetSystem);
  server.on("/get_mode", HTTP_GET, handleGetMode);
  server.on("/set_mode", HTTP_GET, handleSetMode);
  server.begin();
#endif

  // OTA 检查互斥锁（用于后台任务写入状态）
  settingsOtaMutex = xSemaphoreCreateMutex();

  // BLE: minimal GATT skeleton (advertise + connect + RX->TX notify OK)
  cody_ble_init();
  ble_image::init();
  ble_ota::init();
}

static void settingsEnterMenu() {
  settingsFormatHoldActive = false;
  settingsFormatHoldFullPhase = false;
  invalidateSettingsMenuLayout();
  settingsActive = true;
  settingsPage = SET_PAGE_MENU;
  settingsSelected = 0;
  settingsSubSelected = 0;
  drawSettingsMenu(settingsSelected);
}

static void settingsExit() {
  settingsFormatHoldActive = false;
  settingsFormatHoldFullPhase = false;
  settingsActive = false;
  refreshDisplayByMode();
}

static void drawWifiDebugDisabledHint(const char* title) {
  tft.fillScreen(ST77XX_BLACK);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(ST77XX_CYAN);
  u8g2.setCursor(10, 36);
  u8g2.print(title ? title : "WiFi");
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(10, 76);
  u8g2.print("此固件已关闭");
  u8g2.setCursor(10, 98);
  u8g2.print("WiFi/Web 调试功能");
  u8g2.setForegroundColor(0xAD55);
  u8g2.setCursor(10, 140);
  u8g2.print("请用 BLE 小程序控制");
  delay(900);
  settingsShowCurrentPage();
}

static void performSettingsFormat() {
  drawHoldProgressReset();
  // 禁止在此处栈上构造 WiFiManager：体积大，ESP32-C3 易栈溢出导致无法开机/反复复位
  #if CODY_ENABLE_WIFI_DEBUG
  factoryResetWifiCredentials();
  #endif
  delay(200);
  LittleFS.format();
  if (!LittleFS.begin(true)) {
    LittleFS.format();
    LittleFS.begin(true);
  }
  currentImageIndex = 0;
  imageCount = 0;
  switchInterval = DEFAULT_INTERVAL;
  slideshowEnabled = true;
  displayMode = 0;
  pinnedNoteIndex = -1;
  noteSlideshowEnabled = false;
  noteSwitchInterval = 10;
  if (LittleFS.totalBytes() > 0) {
    saveConfig();
  }
  delay(400);
  ESP.restart();
}

static void settingsShowCurrentPage() {
  if (!settingsActive) return;
  if (settingsFormatHoldActive) return;
  if (settingsPage == SET_PAGE_MENU) drawSettingsMenu(settingsSelected);
  else if (settingsPage == SET_PAGE_ABOUT) drawSettingsAbout(settingsSubSelected);
  else if (settingsPage == SET_PAGE_BLE_INFO) drawSettingsBleInfo(settingsSubSelected);
}

static void restoreMainScreenAfterToast() {
  if (cody_ble_pair_pending()) return;
  if (settingsActive) settingsShowCurrentPage();
  else refreshDisplayByMode();
}

static void cycleDisplayMode() {
  const int nextMode = (displayMode + 1) % 5;
  if (displayMode == 4 && nextMode != 4 && !handdraw_ble_idle_for_ms(150)) {
    return;
  }
  if (displayMode == 4) {
    handdraw_flush_persist_now();
  }
  displayMode = nextMode;
  saveConfig();
  refreshDisplayByMode();
}

// BLE 配对确认 UI：仅在进入确认态时绘制一次；若断开/切换 peer 则重置。
static bool g_blePairPromptDrawn = false;
static String g_blePairPromptPeer = "";
static uint32_t g_blePairPctBucket = 0xFFFFFFFFu;

static void handleButtons() {
  const unsigned long now = millis();

  // BLE 首次配对确认：短按拒绝，长按允许
  if (cody_ble_pair_pending()) {
    const String peer = String(cody_ble_pair_peer());
    // 仅在收到设备名称（pair_hello）后才显示连接确认页
    if (peer.length() == 0) {
      delay(5);
      return;
    }
    if (!g_blePairPromptDrawn || g_blePairPromptPeer != peer) {
      drawBlePairPrompt(peer.c_str());
      g_blePairPromptDrawn = true;
      g_blePairPromptPeer = peer;
    }

    // 用左键确认配对（默认=BOOT；可通过 BTN_LEFT_PIN 覆盖）
    bool bootRaw = digitalRead(BTN_LEFT_PIN); // true=未按, false=按下
    if (bootRaw != bootRawPrev) {
      bootRawPrev = bootRaw;
      bootRawChangeAt = now;
    }
    if ((now - bootRawChangeAt) > BTN_DEBOUNCE_MS && bootStableLevel != bootRaw) {
      bootStableLevel = bootRaw;
      if (bootStableLevel == false) {
        bootIsHolding = true;
        bootHoldStart = now;
        bootLongActionFired = false;
      } else {
        if (!bootIsHolding) return;
        const unsigned long held = now - bootHoldStart;
        bootIsHolding = false;
        // 松手取消（未达到长按阈值且不是短按拒绝）：进度归零但仍停留在确认页
        if (!bootLongActionFired && held > BOOT_SHORT_MAX_MS && held < BOOT_LONG_SETTINGS_MS) {
          g_blePairPctBucket = 0xFFFFFFFFu;
          drawBlePairProgress(0);
          return;
        }
        if (!bootLongActionFired && held <= BOOT_SHORT_MAX_MS) {
          cody_ble_pair_reject();
          g_blePairPromptDrawn = false;
          g_blePairPromptPeer = "";
          g_blePairPctBucket = 0xFFFFFFFFu;
          drawBlePairProgress(0);
          delay(80);
          restoreMainScreenAfterToast();
          return;
        }
      }
    }

    if (bootIsHolding && !bootLongActionFired) {
      const unsigned long held = now - bootHoldStart;
      // 进度条：每 50ms 刷一次，避免闪屏
      const uint32_t bucket = (uint32_t)(held / 50);
      if (bucket != g_blePairPctBucket) {
        g_blePairPctBucket = bucket;
        const uint32_t p = (held >= BOOT_LONG_SETTINGS_MS) ? 100u : (uint32_t)((held * 100u) / BOOT_LONG_SETTINGS_MS);
        drawBlePairProgress((uint8_t)p);
      }
      if (held >= BOOT_LONG_SETTINGS_MS) {
        bootLongActionFired = true;
        cody_ble_pair_accept();
        g_blePairPromptDrawn = false;
        g_blePairPromptPeer = "";
        g_blePairPctBucket = 0xFFFFFFFFu;
        // 恢复到进入确认前的页面（设置页/模式页）
        delay(80);
        restoreMainScreenAfterToast();
        return;
      }
    }
    return;
  }
  // 离开确认态：允许下次重新绘制确认页
  if (g_blePairPromptDrawn) {
    g_blePairPromptDrawn = false;
    g_blePairPromptPeer = "";
  }

  // --- BOOT（短按：切换选项/切换模式；长按：进入设置/执行选项） ---
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
      bootLongActionFired = false;
      g_settingsLongPressThrottle = 0xFFFF; // 新一次按住，避免与上次节流值相等导致长按进度从不刷新
    } else { // 松开
      if (!bootIsHolding) return;
      const unsigned long held = now - bootHoldStart;
      const bool fired = bootLongActionFired;

      bootIsHolding = false;
      bootLongActionFired = false;

      if (!fired && held <= BOOT_SHORT_MAX_MS) {
        // 短按：设置内切换选项；否则切换显示模式
        if (settingsActive) {
          if (settingsFormatHoldActive) return;
          // 配网模式页：短按退出配网并回到设置菜单
          #if CODY_ENABLE_WIFI_DEBUG
          if (isWifiConfigPortalActive()) {
            wifiConfigPortalStopFromUser();
            drawSettingsMenu(settingsSelected);
            return;
          }
          #endif
          // 关于本机：短按直接返回（按你的需求）
          if (settingsPage == SET_PAGE_ABOUT) {
            settingsPage = SET_PAGE_MENU;
            settingsSubSelected = 0;
            drawSettingsMenu(settingsSelected);
            return;
          }
          // 连接说明：若已绑定设备，则短按在“返回/删除信任设备”间切换；否则短按直接返回
          if (settingsPage == SET_PAGE_BLE_INFO) {
            if (cody_ble_has_trusted()) {
              settingsSubSelected = (settingsSubSelected + 1) % 2;
              drawSettingsBleInfo(settingsSubSelected);
              return;
            }
            settingsPage = SET_PAGE_MENU;
            settingsSubSelected = 0;
            drawSettingsMenu(settingsSelected);
            return;
          }
          if (settingsPage == SET_PAGE_SOFT_UPDATE) {
            settingsPage = SET_PAGE_MENU;
            settingsSubSelected = 0;
            drawSettingsMenu(settingsSelected);
            return;
          }
          if (settingsPage == SET_PAGE_MENU) {
            settingsSelected = (settingsSelected + 1) % 3;
            drawSettingsMenu(settingsSelected);
          } else {
            int optCount = 2;
            if (settingsPage == SET_PAGE_BLE_INFO) optCount = cody_ble_has_trusted() ? 2 : 1;
            else if (settingsPage == SET_PAGE_ABOUT) optCount = 1;  // 仅返回
            else if (settingsPage == SET_PAGE_SOFT_UPDATE) optCount = 1;

            settingsSubSelected = (settingsSubSelected + 1) % optCount;

            if (settingsPage == SET_PAGE_ABOUT) {
              drawSettingsAbout(settingsSubSelected);
            } else if (settingsPage == SET_PAGE_BLE_INFO) {
              drawSettingsBleInfo(settingsSubSelected);
            } else {
              // 其它页面不再提供入口，统一回菜单
              settingsPage = SET_PAGE_MENU;
              settingsSubSelected = 0;
              drawSettingsMenu(settingsSelected);
            }
          }
        } else {
          cycleDisplayMode();
        }
      } else {
        // 非短按：未触发长按时回到当前页（如介于短按/长按阈值之间的松手）
        // 长按已触发（fired）时，松手不要再整页重绘：子页/主菜单已在按下阈值到达时画过，否则会闪屏
        if (settingsActive) {
          if (!fired && !settingsFormatHoldActive) {
            // 介于短按与长按之间松手：仅清除选中行上的长按进度，勿 invalidate 整屏（会 fillScreen 黑闪）
            if (held > BOOT_SHORT_MAX_MS && held < BOOT_LONG_SETTINGS_MS) {
              if (held >= SETTINGS_LONG_PRESS_PROGRESS_DELAY_MS) {
                if (settingsPage == SET_PAGE_MENU) {
                  drawSettingsMenuClearLongPressProgress(settingsSelected);
                } else {
                  settingsShowCurrentPage();
                }
              } else {
                settingsShowCurrentPage();
              }
            } else {
              settingsShowCurrentPage();
            }
          }
        #if CODY_ENABLE_WIFI_DEBUG
        } else if (!isWifiConfigPortalActive()) {
          if (!fired) refreshDisplayByMode();
        #else
        } else {
          if (!fired) refreshDisplayByMode();
        #endif
        }
      }
    }
  }

  // 长按过程（不等松开）：达到阈值立即执行动作
  if (bootIsHolding && !bootLongActionFired) {
    unsigned long held = now - bootHoldStart;
    if (held >= BOOT_LONG_SETTINGS_MS) {
      bootLongActionFired = true;

      if (!settingsActive) {
        settingsEnterMenu();
        return;
      }

      // 设置页内：长按执行当前选项
      if (settingsPage == SET_PAGE_MENU) {
        if (settingsSelected == 0) { // 退出
          settingsExit();
        } else if (settingsSelected == 1) { // 蓝牙信息
          settingsPage = SET_PAGE_BLE_INFO;
          settingsSubSelected = 0;
          drawSettingsBleInfo(settingsSubSelected);
        } else if (settingsSelected == 2) {
          settingsPage = SET_PAGE_ABOUT;
          settingsSubSelected = 0;
          drawSettingsAbout(settingsSubSelected);
        }
      } else if (settingsPage == SET_PAGE_BLE_INFO) {
        if (cody_ble_has_trusted() && settingsSubSelected == 1) {
          // 长按删除信任设备：清空绑定并断开蓝牙
          cody_ble_clear_trusted_and_disconnect();
          settingsSubSelected = 0;
          drawSettingsBleInfo(settingsSubSelected);
        } else {
          settingsPage = SET_PAGE_MENU;
          settingsSubSelected = 0;
          drawSettingsMenu(settingsSelected);
        }
      } else if (settingsPage == SET_PAGE_SOFT_UPDATE) {
        settingsPage = SET_PAGE_MENU;
        settingsSubSelected = 0;
        drawSettingsMenu(settingsSelected);
      } else if (settingsPage == SET_PAGE_ABOUT) {
        // 关于本机：短按在“返回/退出设置”间切换，长按执行
        settingsPage = SET_PAGE_MENU;
        settingsSubSelected = 0;
        drawSettingsMenu(settingsSelected);
      }
      g_settingsLongPressThrottle = 0xFFFF;
    } else {
      // 未达长按阈值：满 SETTINGS_LONG_PRESS_PROGRESS_DELAY_MS 后，选中项背景从左向右作为进度
      #if CODY_ENABLE_WIFI_DEBUG
      if (settingsActive && !settingsFormatHoldActive && !isWifiConfigPortalActive()) {
      #else
      if (settingsActive && !settingsFormatHoldActive) {
      #endif
        if (held < SETTINGS_LONG_PRESS_PROGRESS_DELAY_MS) {
          // 延迟段内不绘进度，保持 drawSettingsMenu 的整块选中色
        } else if (settingsPage == SET_PAGE_MENU) {
          const unsigned long win = BOOT_LONG_SETTINGS_MS - SETTINGS_LONG_PRESS_PROGRESS_DELAY_MS;
          const float p = win > 0 ? (float)(held - SETTINGS_LONG_PRESS_PROGRESS_DELAY_MS) / (float)win : 1.f;
          const uint16_t th = (uint16_t)((held - SETTINGS_LONG_PRESS_PROGRESS_DELAY_MS) / SETTINGS_LONG_PRESS_THROTTLE_MS);
          if (th != g_settingsLongPressThrottle) {
            g_settingsLongPressThrottle = th;
            drawSettingsMenuLongPressProgress(settingsSelected, p);
          }
        } else if (settingsPage == SET_PAGE_BLE_INFO) {
          // 连接说明页：对“返回/删除信任设备”绘制长按进度
          if (cody_ble_has_trusted()) {
            const unsigned long win = BOOT_LONG_SETTINGS_MS - SETTINGS_LONG_PRESS_PROGRESS_DELAY_MS;
            const float p = win > 0 ? (float)(held - SETTINGS_LONG_PRESS_PROGRESS_DELAY_MS) / (float)win : 1.f;
            const uint16_t th = (uint16_t)((held - SETTINGS_LONG_PRESS_PROGRESS_DELAY_MS) / SETTINGS_LONG_PRESS_THROTTLE_MS);
            if (th != g_settingsLongPressThrottle) {
              g_settingsLongPressThrottle = th;
              drawSettingsBleInfoLongPressProgress(settingsSubSelected, p);
            }
          }
        }
      }
    }
  }

  if (!bootIsHolding) g_settingsLongPressThrottle = 0xFFFF;
}

void loop() {
  #if CODY_ENABLE_WIFI_DEBUG
  server.handleClient();
  #endif
  cody_ble_loop();
  ble_image::loop();
  ble_ota::loop();
  handdraw_idle_tick();

  // 若从“配对确认态”恢复（比如手机断开/用户在小程序取消），立刻恢复之前 UI。
  static bool s_prevPairPending = false;
  const bool pendingNow = cody_ble_pair_pending();
  if (s_prevPairPending && !pendingNow) {
    g_blePairPromptDrawn = false;
    g_blePairPromptPeer = "";
    restoreMainScreenAfterToast();
  }
  s_prevPairPending = pendingNow;

  // 配网门户运行时先处理按键再跑 WiFiManager::process()（HTTP/DNS），避免「短按返回」被 process 拖慢
  #if CODY_ENABLE_WIFI_DEBUG
  const bool portalAtStart = isWifiConfigPortalActive();
  #else
  const bool portalAtStart = false;
  #endif
  if (portalAtStart) {
    handleButtons();
  }

  #if CODY_ENABLE_WIFI_DEBUG
  serviceWifiConfigPortal();
  if (wifiConfigPortalConsumeSuccessExit()) {
    tft.fillScreen(ST77XX_BLACK);
    u8g2.setFont(u8g2_font_wqy16_t_gb2312);
    u8g2.setFontMode(1);
    const char* okMsg = "配网成功";
    int w = u8g2.getUTF8Width(okMsg);
    u8g2.setForegroundColor(ST77XX_GREEN);
    u8g2.setCursor((240 - w) / 2, 108);
    u8g2.print(okMsg);
    const char* subMsg = "已连接 WiFi";
    w = u8g2.getUTF8Width(subMsg);
    u8g2.setForegroundColor(0xAD55);
    u8g2.setCursor((240 - w) / 2, 136);
    u8g2.print(subMsg);
    delay(2000);
    settingsExit();
    refreshDisplayByMode();
  } else if (wifiConfigPortalConsumeRedrawFlag() && settingsActive) {
    drawSettingsMenu(settingsSelected);
  }
  #endif

  if (!portalAtStart) {
    handleButtons();
  }

  // BLE 配对确认弹窗期间：已允许 handleButtons() 绘制/处理按键；
  // 这里阻止后续逻辑刷新屏幕（避免被模式/提示条覆盖）。
  if (cody_ble_pair_pending()) {
    delay(5);
    return;
  }

  if (settingsFormatHoldActive) {
    const bool bootDown = (digitalRead(BTN_BOOT_PIN) == LOW);
    if (!bootDown) {
      settingsFormatHoldActive = false;
      settingsFormatHoldFullPhase = false;
      drawHoldProgressReset();
      drawSettingsMenu(settingsSelected);
    } else {
      const uint32_t elapsed = (uint32_t)(millis() - settingsFormatHoldStartMs);
      const uint32_t total = SETTINGS_FORMAT_HOLD_MS;
      if (elapsed >= total) {
        drawSettingsEraseHoldProgress(total, total);
        if (!settingsFormatHoldFullPhase) {
          settingsFormatHoldFullPhase = true;
          settingsFormatHoldFullSinceMs = millis();
        } else if ((millis() - settingsFormatHoldFullSinceMs) >= SETTINGS_FORMAT_PAUSE_AT_FULL_MS) {
          settingsFormatHoldActive = false;
          settingsFormatHoldFullPhase = false;
          performSettingsFormat();
        }
      } else {
        drawSettingsEraseHoldProgress(elapsed, total);
      }
    }
  }

  // 首次收到串口数据（如电脑端连上后发首字节）：叠画提示 3 秒，不重启、不阻塞
  if (!g_pcSerialFirstRxSeen && Serial.available() > 0) {
    g_pcSerialFirstRxSeen = true;
    g_pcSerialToastEnd = millis() + 3000;
    g_pcSerialToastOverlayDrawn = false;
  }

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

  #if CODY_ENABLE_WIFI_DEBUG
  if (isTryingNewWifi) {
    unsigned long elapsed = millis() - wifiTryStart;
    String tgt = targetSSID;
    tgt.trim();
    if (tgt.length() == 0) {
      isTryingNewWifi = false;
    } else {
      bool linked = (WiFi.status() == WL_CONNECTED);
      bool ipOk = (WiFi.localIP()[0] != 0);
      String cur = WiFi.SSID();
      cur.trim();
      // SSID 一致；或驱动暂时返回空字符串（ESP 常见）；或已联网+有 IP 较久仍字符串不一致（UTF-8/编码边缘）
      bool nameMatch = (cur.length() == 0) || (cur == tgt);
      bool longSettle = (elapsed >= 12000 && linked && ipOk);

      if (elapsed > 2000 && linked && ipOk && (nameMatch || longSettle)) {
        emitWifiJoinResultEvent(true);
        isTryingNewWifi = false;
      } else if (elapsed > 28000) {
        WiFi.disconnect();
        delay(500);
        if (fallbackSSID.length() > 0) {
          WiFi.begin(fallbackSSID.c_str(), fallbackPSK.c_str());
        } else {
          WiFi.begin();
        }
        delay(800);
        emitWifiJoinResultEvent(false);
        isTryingNewWifi = false;
      }
    }
  }

  // 设置-软件更新：后台任务完成后刷新 UI（主线程始终可操作）
  if (settingsActive && settingsPage == SET_PAGE_SOFT_UPDATE && settingsOtaState == OTA_DONE) {
    if (settingsOtaMutex) xSemaphoreTake(settingsOtaMutex, portMAX_DELAY);
    settingsOtaState = OTA_IDLE;
    const bool avail = settingsUpdateAvailable;
    const String latest = settingsLatestVer;
    const String notes = settingsNotes;
    if (settingsOtaMutex) xSemaphoreGive(settingsOtaMutex);

    drawSettingsSoftwareUpdate(settingsSubSelected, CURRENT_VERSION,
                               latest.c_str(), avail,
                               settingsSoftUpdateHintForDraw(),
                               notes.c_str());
  }

  // 开机 WiFi：只尝试一次，超时后停止
  if (bootWifiAttemptActive && !bootWifiAttemptDone) {
    if (WiFi.status() == WL_CONNECTED) {
      bootWifiAttemptDone = true;
      bootWifiAttemptActive = false;
    } else if (millis() - bootWifiAttemptStart >= BOOT_WIFI_ONESHOT_TIMEOUT_MS) {
      bootWifiAttemptDone = true;
      bootWifiAttemptActive = false;
      // 停止后续反复尝试（保持配置不清除）
      WiFi.disconnect(false, false);
      delay(50);
      esp_wifi_stop();
    }
  }
  #endif
  
  const unsigned long nowToast = millis();
  const bool pcSerialToastActive = (nowToast < g_pcSerialToastEnd);

  if (settingsActive || bootIsHolding || settingsFormatHoldActive) {
    // 设置页/按键按住时，不跑模式刷新
  } else if (displayMode == 1) {
    if (!pcSerialToastActive) {
      time_t now = time(nullptr);
      if (now >= (time_t)1700000000) {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_min != lastMinute) {
          drawClockFaceOnMinuteTick();
        }
      }
    }
  }
  else if (displayMode == 0) {
    if (!pcSerialToastActive && slideshowEnabled && !slideshowPaused && imageCount > 1) {
      if (millis() - lastImageSwitch > switchInterval * 1000) nextImage();
    }
  }
  else if (displayMode == 2) {
    if (!pcSerialToastActive && pinnedNoteIndex == -1 && noteSlideshowEnabled) {
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
    // 提示条显示期间暂停表情动画：避免每帧重画底部与底栏叠画造成严重闪烁
    if (!pcSerialToastActive) expressionModeTick();
  }

  // 电脑已连接：底栏只画一次；结束后整屏恢复
  if (pcSerialToastActive) {
    if (!g_pcSerialToastOverlayDrawn) {
      drawPcSerialToastOverlay();
      g_pcSerialToastOverlayDrawn = true;
    }
    g_pcSerialToastWasShowing = true;
  } else if (g_pcSerialToastWasShowing) {
    g_pcSerialToastWasShowing = false;
    g_pcSerialToastOverlayDrawn = false;
    restoreMainScreenAfterToast();
  }

  // 手绘：处理笔画时主循环其余逻辑可能耗时，末尾再拉一次 BLE，减少「手机已画出一截屏才跟上」的体感延迟
  if (displayMode == 4 && !settingsActive && !cody_ble_pair_pending()) {
    cody_ble_loop();
  }
}