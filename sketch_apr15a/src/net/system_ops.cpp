#include "include/globals.h"
#include "include/net/system_ops.h"
#include "include/render/display_render.h"
#include <WiFi.h>
#include <esp_wifi.h>

static WiFiManager g_wmPortal;
static bool g_portalFromSettings = false;
static bool g_needRedrawAfterPortal = false;
static bool g_portalSuccessNeedExit = false;
/** 门户已结束但 process() 未报成功 / STA 尚未 WL_CONNECTED：在主循环中短暂轮询 */
static bool g_portalAwaitStaAfterClose = false;
static unsigned long g_portalAwaitStaSince = 0;
static const unsigned long PORTAL_STA_WAIT_MS = 8000;

static void configModeCallbackSettings(WiFiManager* myWiFiManager) {
  tft.fillScreen(ST77XX_BLACK);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(ST77XX_CYAN);
  u8g2.setCursor(10, 24);
  u8g2.print("配网模式");
  tft.drawFastHLine(10, 32, 220, 0x4208);

  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(10, 66);
  u8g2.print("1.连接热点");
  u8g2.setForegroundColor(0xAD55);
  u8g2.print("（无密码）");
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.print(":");

  u8g2.setForegroundColor(ST77XX_CYAN);
  u8g2.setCursor(10, 90);
  u8g2.print(myWiFiManager->getConfigPortalSSID());

  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(10, 114);
  u8g2.print("2.浏览器打开:");

  u8g2.setForegroundColor(ST77XX_GREEN);
  u8g2.setCursor(10, 138);
  u8g2.print(WiFi.softAPIP().toString());

  u8g2.setForegroundColor(0xAD55);
  u8g2.setCursor(10, 162);
  u8g2.print("按网页提示选择WiFi");

  tft.fillRect(0, 214, 240, 26, ST77XX_BLACK);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(0xAD55);
  u8g2.setCursor(10, 236);
  u8g2.print("短按返回");
}

void factoryResetWifiCredentials() {
  if (g_wmPortal.getConfigPortalActive()) {
    g_wmPortal.stopConfigPortal();
  }
  g_wmPortal.resetSettings();
  WiFi.disconnect(true, true);
  delay(100);
}

void handleResetSystem() {
  factoryResetWifiCredentials();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
  delay(1000);
  ESP.restart();
}

void startWifiConfigPortalFromSettings() {
  if (g_wmPortal.getConfigPortalActive()) return;

  g_portalAwaitStaAfterClose = false;
  invalidateSettingsMenuLayout();

  server.stop();
  esp_wifi_start();
  WiFi.mode(WIFI_STA);
  delay(50);

  g_wmPortal.setConfigPortalBlocking(false);
  g_wmPortal.setConfigPortalTimeout(300);
  g_wmPortal.setAPCallback(configModeCallbackSettings);
  if (!g_wmPortal.startConfigPortal("Cody-Setup", nullptr)) {
    server.begin();
    return;
  }
  g_portalFromSettings = true;
}

void serviceWifiConfigPortal() {
  // 门户已关闭：等待 STA 晚于 process() 返回值变为已连接（ESP32 上常见）
  if (g_portalAwaitStaAfterClose) {
    if (WiFi.status() == WL_CONNECTED) {
      g_portalAwaitStaAfterClose = false;
      g_portalSuccessNeedExit = true;
    } else if (millis() - g_portalAwaitStaSince >= PORTAL_STA_WAIT_MS) {
      g_portalAwaitStaAfterClose = false;
      g_needRedrawAfterPortal = true;
    }
    return;
  }

  if (!g_wmPortal.getConfigPortalActive()) return;

  const bool procReportsConnected = g_wmPortal.process();

  if (!g_wmPortal.getConfigPortalActive()) {
    server.begin();
    if (g_portalFromSettings) {
      g_portalFromSettings = false;
      const bool staUp = (WiFi.status() == WL_CONNECTED);
      if (procReportsConnected || staUp) {
        g_portalSuccessNeedExit = true;
      } else {
        g_portalAwaitStaAfterClose = true;
        g_portalAwaitStaSince = millis();
      }
    }
  }
}

bool isWifiConfigPortalActive() {
  return g_wmPortal.getConfigPortalActive();
}

void wifiConfigPortalStopFromUser() {
  if (!g_wmPortal.getConfigPortalActive()) return;
  g_portalAwaitStaAfterClose = false;
  g_wmPortal.stopConfigPortal();
  g_portalFromSettings = false;
  // WiFiManager 会关门户与 DNS；再显式关掉 SoftAP，避免个别情况下热点残留
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  server.begin();
}

bool wifiConfigPortalConsumeRedrawFlag() {
  if (!g_needRedrawAfterPortal) return false;
  g_needRedrawAfterPortal = false;
  return true;
}

bool wifiConfigPortalConsumeSuccessExit() {
  if (!g_portalSuccessNeedExit) return false;
  g_portalSuccessNeedExit = false;
  return true;
}
