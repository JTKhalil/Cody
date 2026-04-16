#include "include/globals.h"
#include "include/net/system_ops.h"

void configModeCallback(WiFiManager* myWiFiManager) {
  tft.fillScreen(ST77XX_BLACK);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setForegroundColor(ST77XX_RED);
  u8g2.setCursor(10, 36);
  u8g2.print("连接失败！已开启配网模式");
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(10, 86);
  u8g2.print("1.请连接设备热点:");
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.setCursor(10, 95);
  tft.print(myWiFiManager->getConfigPortalSSID());
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(10, 156);
  u8g2.print("2.手机浏览器打开:");
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(2);
  tft.setCursor(10, 165);
  tft.print(WiFi.softAPIP().toString());
}

void handleResetSystem() {
  WiFiManager wm;
  wm.resetSettings();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
  delay(1000);
  ESP.restart();
}

