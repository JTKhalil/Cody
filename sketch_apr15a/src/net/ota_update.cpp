#include "include/globals.h"
#include "include/net/ota_update.h"

// 由 .ino 提供
extern const char* URL_VERSION;
extern const char* URL_BIN;

void updateProgressCallback(int current, int total) {
  int p = (current * 100) / total;
  static int last = -1;
  if (p == last) return;
  last = p;

  tft.fillRect(10, 180, 220, 20, 0);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setForegroundColor(0x07FF);
  u8g2.setCursor(10, 196);
  u8g2.print("下载进度: ");
  u8g2.print(p);
  u8g2.print("%");
  tft.drawRect(10, 210, 220, 15, 0xFFFF);
  tft.fillRect(12, 212, (216 * p) / 100, 11, 0x07E0);
}

void handleOtaInfo() {
  server.send(200, "application/json",
              "{\"current\":\"" + String(CURRENT_VERSION) + "\",\"url\":\"" + String(URL_VERSION) + "\"}");
}

void handleDoUpdate() {
  server.send(200, "text/plain", "开始更新...");
  delay(1000);
  tft.fillScreen(0);
  u8g2.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2.setForegroundColor(0xFD20);
  u8g2.setCursor(10, 96);
  u8g2.print("系统固件升级中");
  u8g2.setForegroundColor(ST77XX_WHITE);
  u8g2.setCursor(10, 126);
  u8g2.print("请勿切断电源...");

  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpUpdate.onProgress(updateProgressCallback);

  if (httpUpdate.update(client, URL_BIN) == HTTP_UPDATE_FAILED) {
    tft.fillScreen(0);
    u8g2.setForegroundColor(0xF800);
    u8g2.setCursor(10, 96);
    u8g2.print("升级失败!");
    delay(4000);
    ESP.restart();
  }
}

