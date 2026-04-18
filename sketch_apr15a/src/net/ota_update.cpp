#include "include/globals.h"
#include "include/net/ota_update.h"

// 由 .ino 提供
extern const char* URL_VERSION;
extern const char* URL_BIN;

static int parsePart(const String& s) {
  if (s.length() == 0) return 0;
  bool neg = false;
  int i = 0;
  if (s[0] == '-') { neg = true; i = 1; }
  long v = 0;
  for (; i < (int)s.length(); i++) {
    char c = s[i];
    if (c < '0' || c > '9') break;
    v = v * 10 + (c - '0');
    if (v > 1000000) break;
  }
  if (neg) v = -v;
  return (int)v;
}

int compareVersionParts(const char* a, const char* b) {
  String sa = a ? String(a) : String("0");
  String sb = b ? String(b) : String("0");
  sa.trim(); sb.trim();

  int ai = 0, bi = 0;
  for (int k = 0; k < 4; k++) {
    int anext = sa.indexOf('.', ai);
    int bnext = sb.indexOf('.', bi);
    String apart = (anext < 0) ? sa.substring(ai) : sa.substring(ai, anext);
    String bpart = (bnext < 0) ? sb.substring(bi) : sb.substring(bi, bnext);
    int av = parsePart(apart);
    int bv = parsePart(bpart);
    if (av > bv) return 1;
    if (av < bv) return -1;
    if (anext < 0 && bnext < 0) break;
    ai = (anext < 0) ? sa.length() : (anext + 1);
    bi = (bnext < 0) ? sb.length() : (bnext + 1);
  }
  return 0;
}

static String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 16);
  for (int i = 0; i < (int)s.length(); i++) {
    char c = s[i];
    if (c == '\\') out += "\\\\";
    else if (c == '"') out += "\\\"";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') {}
    else if (c == '\t') out += "\\t";
    else out += c;
  }
  return out;
}

bool fetchRemoteVersionInfo(String& outLatest, String& outNotes) {
  outLatest = "";
  outNotes = "";
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(2200);
  if (!http.begin(client, URL_VERSION)) {
    http.end();
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();
  body.replace("\r\n", "\n");

  int nl = body.indexOf('\n');
  if (nl < 0) {
    outLatest = body;
    outLatest.trim();
    return outLatest.length() > 0;
  }
  outLatest = body.substring(0, nl);
  outLatest.trim();
  outNotes = body.substring(nl + 1);
  outNotes.trim();
  // 限制一下长度，避免 UI/JSON 过大
  if (outNotes.length() > 600) outNotes = outNotes.substring(0, 600);
  return outLatest.length() > 0;
}

static void updateProgressCallback(int current, int total) {
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
  String latest, notes;
  bool ok = fetchRemoteVersionInfo(latest, notes);
  bool available = false;
  if (ok) available = (compareVersionParts(latest.c_str(), CURRENT_VERSION) > 0);

  String payload = "{";
  payload += "\"current\":\"" + String(CURRENT_VERSION) + "\"";
  payload += ",\"latest\":\"" + String(ok ? latest : "") + "\"";
  payload += ",\"available\":" + String(available ? "true" : "false");
  payload += ",\"notes\":\"" + jsonEscape(ok ? notes : "") + "\"";
  payload += ",\"url\":\"" + String(URL_VERSION) + "\"";
  payload += "}";
  server.send(200, "application/json", payload);
}

void startOtaUpdate() {
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

void handleDoUpdate() {
  server.send(200, "text/plain", "开始更新...");
  delay(150);
  startOtaUpdate();
}

