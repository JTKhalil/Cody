#include "include/globals.h"
#include "include/protocol/serial_protocol.h"
#if CODY_ENABLE_WIFI_DEBUG
  #include <WiFi.h>
#endif
#include "include/util_hex.h"
#include "include/core/config_store.h"
#include "include/storage/image_store.h"
#include "include/render/display_render.h"
#include "include/render/handdraw.h"
#include "include/render/guess_game.h"
#include "include/net/ota_update.h"
#include "include/net/system_ops.h"
#include "include/ble/cody_ble.h"
#include "include/ble/ble_image.h"
#include <Update.h>
#include "mbedtls/base64.h"

static ProtocolLineEmitter g_lineEmitter = nullptr;

void serial_protocol_set_line_emitter(ProtocolLineEmitter emitter) {
  g_lineEmitter = emitter;
}

void serial_protocol_emit_line(const String& line) {
  Serial.println(line);
  if (g_lineEmitter) g_lineEmitter(line);
}

static void emitJsonDocLine(JsonDocument& doc) {
  // Avoid serializeJson(doc, String&) to keep code size down.
  // Most protocol responses are small; if a doc doesn't fit, we still print to Serial,
  // but skip forwarding to BLE emitter (which is size-limited anyway).
  char buf[512];
  const size_t n = serializeJson(doc, buf, sizeof(buf));
  if (n == 0 || n >= sizeof(buf)) {
    serializeJson(doc, Serial);
    Serial.println();
    return;
  }

  Serial.write((const uint8_t*)buf, n);
  Serial.println();
  if (g_lineEmitter) g_lineEmitter(String(buf));
}

void emitWifiJoinResultEvent(bool ok) {
  DynamicJsonDocument doc(512);
  doc["cmd"] = "wifi_join_result";
  doc["status"] = "ok";
  doc["ok"] = ok;
  #if CODY_ENABLE_WIFI_DEBUG
    doc["connected"] = (WiFi.status() == WL_CONNECTED);
    doc["ssid"] = WiFi.SSID();
    doc["ip"] = WiFi.localIP().toString();
  #else
    doc["connected"] = false;
    doc["ssid"] = "";
    doc["ip"] = "";
    doc["msg"] = "wifi_debug_disabled";
  #endif
  emitJsonDocLine(doc);
}

// 串口推送固件（由 PC 下载后分块写入 OTA 分区）
static bool gOtaSerialActive = false;
static size_t gOtaSerialTotal = 0;
static size_t gOtaSerialWritten = 0;

void processSerialCommand(const String& payload) {
  // ota_serial_chunk 单行含 ~512 字符 hex（256B）+ 键名；4096 在部分堆碎片情况下可能截断 data，导致奇数长度 → bad hex
  DynamicJsonDocument doc(12288);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    serial_protocol_emit_line("{\"cmd\":\"error\",\"status\":\"error\",\"msg\":\"JSON Parse Error\"}");
    return;
  }

  String cmd = doc["cmd"].as<String>();

  if (cmd == "get_notes") {
    // 注意：该响应可能很长（notes 数组），必须通过 serial_protocol_emit_line 转发到 BLE，
    // 不能仅 Serial.print，否则 WxCody 无法收到回包。
    String notesJson = "[]";
    if (LittleFS.exists("/notes.json")) {
      File f = LittleFS.open("/notes.json", "r");
      if (f) {
        const size_t sz = f.size();
        // 防御：避免极端情况下构造超大 String 造成内存碎片或 OOM
        if (sz > 64 * 1024) {
          f.close();
          serial_protocol_emit_line("{\"cmd\":\"get_notes\",\"status\":\"error\",\"msg\":\"notes_too_large\"}");
          return;
        }
        notesJson = "";
        notesJson.reserve((unsigned int)sz + 8);
        while (f.available()) notesJson += (char)f.read();
        f.close();
      }
    }

    String line;
    line.reserve((unsigned int)128 + (unsigned int)notesJson.length());
    line += "{\"cmd\":\"get_notes\",\"status\":\"ok\",\"noteSlideshow\":";
    line += (noteSlideshowEnabled ? "true" : "false");
    line += ",\"noteInterval\":";
    line += String(noteSwitchInterval);
    line += ",\"pinned\":";
    line += String(pinnedNoteIndex);
    line += ",\"notes\":";
    line += notesJson;
    line += "}";

    serial_protocol_emit_line(line);
    return;
  }

  if (cmd == "get_image_chunk") {
    int s = doc["slot"].as<int>();
    int y = doc["y"].as<int>();
    int h = doc["h"].as<int>();
    String path = "/img" + String(s) + ".bin";
    if (!LittleFS.exists(path)) {
      Serial.println("{\"cmd\":\"image_chunk\",\"status\":\"error\",\"msg\":\"Not Found\"}");
      return;
    }
    File f = LittleFS.open(path, "r");
    f.seek(y * 240 * 2);

    Serial.print("{\"cmd\":\"image_chunk\",\"status\":\"ok\",\"slot\":");
    Serial.print(s);
    Serial.print(",\"y\":");
    Serial.print(y);
    Serial.print(",\"h\":");
    Serial.print(h);
    Serial.print(",\"data\":\"");

    uint16_t buf[240];
    const char hexChars[] = "0123456789abcdef";
    char hexOutput[961];

    for (int row = 0; row < h; row++) {
      int readBytes = f.read((uint8_t*)buf, 480);
      int pixelCount = readBytes / 2;
      for (int i = 0; i < pixelCount; i++) {
        uint16_t p = buf[i];
        hexOutput[i * 4] = hexChars[(p >> 12) & 0x0F];
        hexOutput[i * 4 + 1] = hexChars[(p >> 8) & 0x0F];
        hexOutput[i * 4 + 2] = hexChars[(p >> 4) & 0x0F];
        hexOutput[i * 4 + 3] = hexChars[p & 0x0F];
      }
      hexOutput[pixelCount * 4] = '\0';
      Serial.print(hexOutput);
      yield();
    }
    f.close();
    Serial.println("\"}");
    return;
  }

  if (cmd == "handdraw_meta") {
    uint16_t bg565 = 0;
    bool hasArt = false;
    if (LittleFS.exists("/handdraw_state.txt")) {
      File f = LittleFS.open("/handdraw_state.txt", "r");
      if (f) {
        String line = f.readStringUntil('\n');
        f.close();
        line.trim();
        const int sp = line.indexOf(' ');
        if (sp > 0) {
          bg565 = (uint16_t)line.substring(0, sp).toInt();
          hasArt = line.substring((unsigned int)(sp + 1)).toInt() != 0;
        } else if (line.length() > 0) {
          bg565 = (uint16_t)line.toInt();
        }
      }
    } else if (LittleFS.exists("/handdraw.bin")) {
      File f = LittleFS.open("/handdraw.bin", "r");
      if (f && f.size() == (long)(240 * 240 * 2)) hasArt = true;
      if (f) f.close();
    }
    String out = "{\"cmd\":\"handdraw_meta\",\"status\":\"ok\",\"bg\":\"";
    out += (bg565 == (uint16_t)0x0000) ? "black" : "white";
    out += "\",\"has_art\":";
    out += hasArt ? "true" : "false";
    out += "}";
    serial_protocol_emit_line(out);
    return;
  }

  if (cmd == "handdraw_pull_chunk") {
    // 不强制 mode4：从 Flash/RAM 缓冲读取，便于与 set_mode 并行以缩短小程序首屏
    const size_t off = (size_t)doc["off"].as<unsigned long>();
    int lenReq = doc["len"].as<int>();
    if (lenReq <= 0 || lenReq > 720) lenReq = 720;
    uint8_t buf[720];
    const size_t n = handdraw_copy_pixels(buf, off, (size_t)lenReq);
    String line;
    line.reserve((unsigned int)(48 + n * 2));
    line += "{\"cmd\":\"handdraw_pull_chunk\",\"status\":\"ok\",\"off\":";
    line += String((unsigned long)off);
    line += ",\"len\":";
    line += String((unsigned int)n);
    line += ",\"data\":\"";
    static const char hc[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
      line += hc[(buf[i] >> 4) & 15];
      line += hc[buf[i] & 15];
    }
    line += "\"}";
    serial_protocol_emit_line(line);
    return;
  }

  DynamicJsonDocument resDoc(4096);
  resDoc["cmd"] = cmd;
  resDoc["status"] = "ok";

  if (cmd == "sync_time") {
    // 提示：小程序开始同步时间
    if (displayMode == 1 && !settingsActive) {
      drawTimeSyncingHint();
    }
    long epoch = doc["timestamp"].as<long>();
    struct timeval tv;
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
    // 标记：时间已由外部（BLE/串口）校准
    g_timeCalibrated = true;
    // 若当前在时钟模式，立刻刷新一次界面（不等分钟 tick）
    if (displayMode == 1 && !settingsActive) {
      drawClockFace();
      lastMinute = -1;
    }
  } else if (cmd == "set_wifi") {
    #if !CODY_ENABLE_WIFI_DEBUG
    resDoc["status"] = "error";
    resDoc["msg"] = "wifi_debug_disabled";
    #else
    String newSsid = doc["ssid"].as<String>();
    String newPsk = doc["psk"].as<String>();
    newSsid.trim();
    newPsk.trim();

    wifi_config_t conf;
    esp_wifi_get_config(WIFI_IF_STA, &conf);
    fallbackSSID = String((char*)conf.sta.ssid);
    fallbackPSK = String((char*)conf.sta.password);

    targetSSID = newSsid;

    // 开机 WiFi 超时后会 esp_wifi_stop()；仅 disconnect+begin 无法再起 STA，需先恢复栈（与 system_ops 门户一致）
    esp_wifi_start();
    WiFi.mode(WIFI_STA);
    delay(50);
    WiFi.disconnect();
    delay(100);
    WiFi.begin(newSsid.c_str(), newPsk.c_str());

    isTryingNewWifi = true;
    wifiTryStart = millis();
    #endif
  } else if (cmd == "wifi_status") {
    #if CODY_ENABLE_WIFI_DEBUG
    resDoc["connected"] = (WiFi.status() == WL_CONNECTED);
    resDoc["ssid"] = WiFi.SSID();
    resDoc["ip"] = WiFi.localIP().toString();
    #else
    resDoc["connected"] = false;
    resDoc["ssid"] = "";
    resDoc["ip"] = "";
    resDoc["msg"] = "wifi_debug_disabled";
    #endif
  } else if (cmd == "uptime") {
    resDoc["ms"] = millis();
  } else if (cmd == "get_mode") {
    resDoc["mode"] = displayMode;
    resDoc["guess_show_answer"] = guess_game_is_showing_answer();
  } else if (cmd == "set_mode") {
    int nM = doc["mode"].as<int>();
    if (nM >= 0 && nM <= 4) {
      if (guess_game_is_playing() && nM != displayMode) {
        resDoc["status"] = "error";
        resDoc["msg"] = "guess_game_active";
      } else {
        const bool leavingDraw = (displayMode == 4 && nM != 4);
        if (leavingDraw && !handdraw_ble_idle_for_ms(150)) {
          resDoc["status"] = "error";
          resDoc["msg"] = "handdraw_transfer_busy";
        } else {
          if (leavingDraw) {
            handdraw_flush_persist_now();
            guess_game_reset();
          }
          displayMode = nM;
          saveConfig();
          if (!settingsActive) {
            refreshDisplayByMode();
          }
        }
      }
    } else {
      resDoc["status"] = "error";
    }
  } else if (cmd == "draw_stroke") {
    if (displayMode != 4) {
      resDoc["status"] = "error";
      resDoc["msg"] = "need_mode_4";
    } else if (guess_game_is_showing_answer()) {
      resDoc["status"] = "error";
      resDoc["msg"] = "guess_show_answer";
    } else {
      int x0 = doc["x0"].as<int>();
      int y0 = doc["y0"].as<int>();
      int x1 = doc["x1"].as<int>();
      int y1 = doc["y1"].as<int>();
      uint32_t cu = doc["c"].as<unsigned long>();
      uint16_t c = (uint16_t)(cu & 0xffffu);
      int w = doc["w"].as<int>();
      handdraw_draw_segment(x0, y0, x1, y1, c, w);
      handdraw_notify_ble_stroke_received();
    }
  } else if (cmd == "handdraw_clear") {
    if (displayMode != 4) {
      resDoc["status"] = "error";
      resDoc["msg"] = "need_mode_4";
    } else {
      handdraw_clear_ram();
    }
  } else if (cmd == "guess_game_start") {
    if (displayMode != 4) {
      resDoc["status"] = "error";
      resDoc["msg"] = "need_mode_4";
    } else {
      String w = doc["word"].as<String>();
      w.trim();
      int sec = doc.containsKey("seconds") ? doc["seconds"].as<int>() : 180;
      if (sec < 10) sec = 10;
      if (sec > 600) sec = 600;
      if (w.length() == 0) {
        resDoc["status"] = "error";
        resDoc["msg"] = "need_word";
      } else {
        guess_game_reset();
        handdraw_clear_ram();
        guess_game_start(w.c_str(), (uint16_t)sec);
      }
    }
  } else if (cmd == "guess_game_end") {
    if (displayMode != 4) {
      resDoc["status"] = "error";
      resDoc["msg"] = "need_mode_4";
    } else {
      // reveal 缺省 true：结束本局并揭晓（与旧版小程序一致）
      // reveal false：仅收起倒计时与答案条（退出页面/切后台/跳过换词），不揭晓
      const bool reveal = doc["reveal"] | true;
      if (reveal) {
        guess_game_end_round();
      } else {
        guess_game_reset();
        handdraw_redraw_only();
      }
    }
  } else if (cmd == "handdraw_save") {
    if (displayMode != 4) {
      resDoc["status"] = "error";
      resDoc["msg"] = "need_mode_4";
    } else if (!handdraw_save_to_file()) {
      resDoc["status"] = "error";
      resDoc["msg"] = "write_fail";
    }
  } else if (cmd == "handdraw_delete") {
    if (displayMode != 4) {
      resDoc["status"] = "error";
      resDoc["msg"] = "need_mode_4";
    } else {
      handdraw_delete_saved();
    }
  } else if (cmd == "handdraw_set_bg") {
    if (displayMode != 4) {
      resDoc["status"] = "error";
      resDoc["msg"] = "need_mode_4";
    } else {
      String bgs = doc["bg"].as<String>();
      bgs.trim();
      const bool wantBlack = (bgs == "black");
      const bool wantWhite = (bgs == "white");
      if (!wantBlack && !wantWhite) {
        resDoc["status"] = "error";
        resDoc["msg"] = "bad_bg";
      } else if (!handdraw_set_background_bw(wantBlack)) {
        resDoc["status"] = "error";
        resDoc["msg"] = "bg_locked";
      }
    }
  } else if (cmd == "handdraw_status") {
    if (displayMode != 4) {
      resDoc["status"] = "error";
      resDoc["msg"] = "need_mode_4";
    } else {
      const uint16_t bg = handdraw_get_background_rgb565();
      resDoc["bg"] = (bg == (uint16_t)0x0000) ? "black" : "white";
      const bool locked = handdraw_has_locked_background();
      resDoc["bg_locked"] = locked;
      resDoc["can_change_bg"] = !locked;
    }
  } else if (cmd == "fs_space") {
    size_t t = LittleFS.totalBytes(), u = LittleFS.usedBytes();
    resDoc["total"] = t / 1024;
    resDoc["used"] = u / 1024;
    resDoc["free"] = (t - u) / 1024;
  } else if (cmd == "bright") {
    int v = constrain(doc["v"].as<int>(), 0, 255);
    backlightValue = v;
    analogWrite(TFT_BLK, (v * v) / 255);
  } else if (cmd == "format_fs") {
    resetUserFilesystemToDefaults();
    if (displayMode == 0) tft.fillScreen(0);
  } else if (cmd == "reset_system") {
    resetUserFilesystemToDefaults();
    if (displayMode == 0) tft.fillScreen(0);
    factoryResetWifiCredentials();
    serial_protocol_emit_line("{\"cmd\":\"reset_system\",\"status\":\"ok\"}");
    delay(1000);
    ESP.restart();
  } else if (cmd == "ble_forget") {
    // Clear trusted devices list so next connect requires on-device confirmation again.
    cody_ble_clear_trusted();
  } else if (cmd == "img_cancel") {
    // Cancel current BLE image upload (keep existing images intact).
    ble_image::cancel_push();
  } else if (cmd == "image_info") {
    scanImages();
    // 动态图库：不再固定 MAX_IMAGES。
    // 返回已有槽位索引列表 + 存储信息 + 是否还能新增一张 + 下一空槽位。
    const size_t totalB = LittleFS.totalBytes();
    const size_t usedB = LittleFS.usedBytes();
    const size_t freeB = (totalB > usedB) ? (totalB - usedB) : 0;
    resDoc["fs_total_b"] = (uint32_t)totalB;
    resDoc["fs_used_b"] = (uint32_t)usedB;
    resDoc["fs_free_b"] = (uint32_t)freeB;

    static constexpr uint32_t kImageBytes = 240u * 240u * 2u;
    static constexpr uint32_t kReserveBytesForAdd = kImageBytes * 2u; // 预留至少两张图的空间，才允许显示“新增空槽位”
    resDoc["img_bytes"] = kImageBytes;
    resDoc["can_add"] = (freeB >= (size_t)kReserveBytesForAdd);
    resDoc["next_slot"] = image_next_free_slot();

    // 兼容旧字段：不再输出巨大布尔数组 slots（避免 BLE JSON 行过大）。
    // 改为 indices：已有图片的 slot 列表。
    JsonArray indices = resDoc.createNestedArray("indices");
    File root = LittleFS.open("/");
    if (root) {
      while (true) {
        File f = root.openNextFile();
        if (!f) break;
        String name = String(f.name());
        size_t sz = f.size();
        f.close();
        if (!name.startsWith("/")) name = "/" + name;
        if (!name.startsWith("/img") || !name.endsWith(".bin")) continue;
        if (sz != kImageBytes) continue;
        String numStr = name.substring(4, name.length() - 4);
        int slot = numStr.toInt();
        if (slot < 0 || slot > 250) continue;
        indices.add(slot);
      }
      root.close();
    }
    resDoc["current"] = currentImageIndex;
    resDoc["slideshow"] = slideshowEnabled;
    resDoc["interval"] = switchInterval;
  } else if (cmd == "set_current_image") {
    int s = doc["slot"].as<int>();
    if (LittleFS.exists("/img" + String(s) + ".bin")) {
      currentImageIndex = s;
      saveConfig();
      if (displayMode == 0) {
        displayImageFromFile(currentImageIndex);
        lastImageSwitch = millis();
      }
    }
  } else if (cmd == "delete_image") {
    int s = doc["slot"].as<int>();
    LittleFS.remove("/img" + String(s) + ".bin");
    scanImages();
    if (currentImageIndex == s && imageCount > 0) nextImage();
    saveConfig();
    if (displayMode == 0) {
      if (imageCount == 0) loadSavedImage(); // 显示「图库空空如也」
      else displayImageFromFile(currentImageIndex);
    }
  } else if (cmd == "set_img_slideshow") {
    if (doc.containsKey("enabled")) slideshowEnabled = doc["enabled"].as<bool>();
    if (doc.containsKey("interval")) switchInterval = constrain(doc["interval"].as<int>(), 3, 60);
    saveConfig();
    if (slideshowEnabled) lastImageSwitch = millis();
  } else if (cmd == "set_interval") {
    // 与 HTTP /interval?value= 对齐：WXCody 发送 { "cmd":"set_interval", "value": N }
    if (doc.containsKey("value")) {
      switchInterval = constrain(doc["value"].as<int>(), 3, 60);
      saveConfig();
      if (slideshowEnabled) lastImageSwitch = millis();
    } else {
      resDoc["status"] = "error";
      resDoc["msg"] = "missing value";
    }
  } else if (cmd == "slideshow_config") {
    // 与 HTTP /slideshow_config 对齐：返回当前图库轮播开关与间隔（秒）
    resDoc["enabled"] = slideshowEnabled;
    resDoc["interval"] = switchInterval;
  } else if (cmd == "upload_start") {
    if (uploadFile) uploadFile.close();
    isUploading = false;
    int s = doc["slot"].as<int>();
    if (s < 0 || s >= MAX_IMAGES) s = 0;
    uploadingSlot = s;

    String targetPath = "/img" + String(s) + ".bin";
    if (LittleFS.exists(targetPath)) LittleFS.remove(targetPath);
    if (LittleFS.exists("/tmp_upload.bin")) LittleFS.remove("/tmp_upload.bin");

    uploadFile = LittleFS.open("/tmp_upload.bin", "w");
    if (!uploadFile) {
      resDoc["status"] = "error";
      resDoc["msg"] = "FS Open Fail";
    } else {
      isUploading = true;
      slideshowPaused = true;
      uploadY = 0;
      totalWritten = 0;
    }
  } else if (cmd == "upload_chunk") {
    if (!isUploading) {
      resDoc["status"] = "error";
      resDoc["msg"] = "Not in upload mode";
    } else {
      int y = doc["y"].as<int>();
      int h = doc["h"].as<int>();
      (void)y;
      String hexData = doc["data"].as<String>();
      const char* hex = hexData.c_str();
      for (int row = 0; row < h; row++) {
        uint16_t lb[240];
        for (int x = 0; x < 240; x++) {
          int i = (row * 240 + x) * 4;
          lb[x] = (charToHex(hex[i]) << 12) | (charToHex(hex[i + 1]) << 8) |
                  (charToHex(hex[i + 2]) << 4) | charToHex(hex[i + 3]);
        }
        totalWritten += uploadFile.write((uint8_t*)lb, 480);
      }
      uploadFile.flush();
      uploadY += h;
    }
  } else if (cmd == "upload_finish") {
    if (!isUploading) {
      resDoc["status"] = "error";
      resDoc["msg"] = "Not in upload mode";
    } else {
      uploadFile.close();
      delay(50);
      String tP = "/img" + String(uploadingSlot) + ".bin";
      LittleFS.remove(tP);
      LittleFS.rename("/tmp_upload.bin", tP);
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
    }
  } else if (cmd == "save_note") {
    DynamicJsonDocument notesDoc(2048);
    if (LittleFS.exists("/notes.json")) {
      File f = LittleFS.open("/notes.json", "r");
      deserializeJson(notesDoc, f);
      f.close();
    } else {
      notesDoc.to<JsonArray>();
    }
    String content = doc["content"].as<String>();
    struct tm timeinfo;
    char timeStr[20];
    if (getLocalTime(&timeinfo)) strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M", &timeinfo);
    else strcpy(timeStr, "串口添加");
    int editIndex = doc.containsKey("index") ? doc["index"].as<int>() : -1;
    if (editIndex >= 0 && editIndex < static_cast<int>(notesDoc.size())) {
      notesDoc[editIndex]["content"] = content;
      notesDoc[editIndex]["time"] = timeStr;
    } else {
      JsonObject obj = notesDoc.createNestedObject();
      obj["content"] = content;
      obj["time"] = timeStr;
    }
    File f = LittleFS.open("/notes.json", "w");
    serializeJson(notesDoc, f);
    f.close();
    if (displayMode == 2) displayNoteOnScreen();
  } else if (cmd == "delete_note") {
    int index = doc["index"].as<int>();
    DynamicJsonDocument notesDoc(2048);
    File f = LittleFS.open("/notes.json", "r");
    deserializeJson(notesDoc, f);
    f.close();
    notesDoc.remove(index);
    if (pinnedNoteIndex == index) {
      pinnedNoteIndex = -1;
      saveConfig();
    } else if (pinnedNoteIndex > index) {
      pinnedNoteIndex--;
      saveConfig();
    }
    f = LittleFS.open("/notes.json", "w");
    serializeJson(notesDoc, f);
    f.close();
    if (displayMode == 2) displayNoteOnScreen();
  } else if (cmd == "set_note_config") {
    if (doc.containsKey("pinned")) pinnedNoteIndex = doc["pinned"].as<int>();
    if (doc.containsKey("slideshow")) noteSlideshowEnabled = doc["slideshow"].as<bool>();
    if (doc.containsKey("interval")) noteSwitchInterval = constrain(doc["interval"].as<int>(), 3, 60);
    saveConfig();
    if (displayMode == 2) {
      lastNoteSwitch = millis();
      displayNoteOnScreen();
    }
  } else if (cmd == "ota_serial_abort") {
    if (gOtaSerialActive) {
      Update.abort();
      gOtaSerialActive = false;
      gOtaSerialTotal = 0;
      gOtaSerialWritten = 0;
    }
  } else if (cmd == "ota_serial_begin") {
    if (gOtaSerialActive) {
      Update.abort();
      gOtaSerialActive = false;
    }
    size_t sz = doc["size"].as<size_t>();
    const size_t kMaxApp = 0x1C0000;
    if (sz < 8192 || sz > kMaxApp) {
      resDoc["status"] = "error";
      resDoc["msg"] = "bad size";
    } else {
      Update.abort();
      if (!Update.begin(sz, U_FLASH)) {
        resDoc["status"] = "error";
        resDoc["msg"] = Update.errorString();
      } else {
        gOtaSerialActive = true;
        gOtaSerialTotal = sz;
        gOtaSerialWritten = 0;
        resDoc["max_chunk"] = 512;
        // PC 若识别到此字段则走 Base64，单行更短、避免 hex 奇数位/截断问题
        resDoc["enc"] = "b64";
      }
    }
  } else if (cmd == "ota_serial_chunk") {
    if (!gOtaSerialActive) {
      resDoc["status"] = "error";
      resDoc["msg"] = "not in OTA";
    } else {
      uint8_t buf[512];
      size_t nbytes = 0;
      bool havePayload = false;

      if (doc.containsKey("b64")) {
        String b64 = doc["b64"].as<String>();
        if (b64.length() == 0) {
          resDoc["status"] = "error";
          resDoc["msg"] = "empty b64";
        } else {
          size_t olen = 0;
          int br = mbedtls_base64_decode(buf, sizeof(buf), &olen,
                                          (const unsigned char*)b64.c_str(),
                                          b64.length());
          if (br != 0) {
            resDoc["status"] = "error";
            resDoc["msg"] = "bad b64";
          } else if (olen == 0 || olen > 512) {
            resDoc["status"] = "error";
            resDoc["msg"] = olen > 512 ? "chunk too big" : "empty chunk";
          } else {
            nbytes = olen;
            havePayload = true;
          }
        }
      } else {
        String hexData = doc["data"].as<String>();
        if (hexData.length() == 0 || (hexData.length() % 2) != 0) {
          resDoc["status"] = "error";
          resDoc["msg"] = String("bad hex len=") + String((unsigned int)hexData.length());
        } else {
          nbytes = hexData.length() / 2;
          if (nbytes > 512) {
            resDoc["status"] = "error";
            resDoc["msg"] = "chunk too big";
          } else {
            const char* h = hexData.c_str();
            for (size_t i = 0; i < nbytes; i++) {
              buf[i] = (charToHex(h[i * 2]) << 4) | charToHex(h[i * 2 + 1]);
            }
            havePayload = true;
          }
        }
      }

      if (havePayload) {
        if (gOtaSerialWritten + nbytes > gOtaSerialTotal) {
          Update.abort();
          gOtaSerialActive = false;
          resDoc["status"] = "error";
          resDoc["msg"] = "overflow";
        } else {
          size_t w = Update.write(buf, nbytes);
          if (w != nbytes) {
            Update.abort();
            gOtaSerialActive = false;
            resDoc["status"] = "error";
            resDoc["msg"] = "write fail";
          } else {
            gOtaSerialWritten += w;
            resDoc["written"] = (uint32_t)gOtaSerialWritten;
            resDoc["total"] = (uint32_t)gOtaSerialTotal;
          }
        }
      }
    }
  } else if (cmd == "ota_serial_finish") {
    if (!gOtaSerialActive) {
      resDoc["status"] = "error";
      resDoc["msg"] = "not in OTA";
    } else if (gOtaSerialWritten != gOtaSerialTotal) {
      Update.abort();
      gOtaSerialActive = false;
      resDoc["status"] = "error";
      resDoc["msg"] = "incomplete";
    } else {
      if (!Update.end(true)) {
        resDoc["status"] = "error";
        resDoc["msg"] = Update.errorString();
        gOtaSerialActive = false;
      } else {
        gOtaSerialActive = false;
        serial_protocol_emit_line("{\"cmd\":\"ota_serial_finish\",\"status\":\"ok\"}");
        delay(500);
        ESP.restart();
        return;
      }
    }
  } else if (cmd == "ota_info") {
    // 版本比对与固件下载由 PC 完成；设备只回报本机版本号
    resDoc["current"] = CURRENT_VERSION;
  } else if (cmd == "do_update") {
    #if CODY_ENABLE_WIFI_DEBUG
      serial_protocol_emit_line("{\"cmd\":\"do_update\",\"status\":\"ok\"}");
      delay(500);
      handleDoUpdate();
      return;
    #else
      resDoc["status"] = "error";
      resDoc["msg"] = "wifi_debug_disabled";
    #endif
  } else if (cmd == "reboot") {
    resDoc["msg"] = "restarting";
    emitJsonDocLine(resDoc);
    delay(200);
    ESP.restart();
    return;
  }

  emitJsonDocLine(resDoc);
}

