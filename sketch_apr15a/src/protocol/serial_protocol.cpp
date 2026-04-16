#include "include/globals.h"
#include "include/protocol/serial_protocol.h"
#include "include/util_hex.h"
#include "include/core/config_store.h"
#include "include/storage/image_store.h"
#include "include/render/display_render.h"
#include "include/net/ota_update.h"

void processSerialCommand(const String& payload) {
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print("{\"cmd\":\"error\",\"status\":\"error\",\"msg\":\"JSON Parse Error\"}");
    Serial.println();
    return;
  }

  String cmd = doc["cmd"].as<String>();

  if (cmd == "get_notes") {
    Serial.print("{\"cmd\":\"get_notes\",\"status\":\"ok\",\"noteSlideshow\":");
    Serial.print(noteSlideshowEnabled ? "true" : "false");
    Serial.print(",\"noteInterval\":");
    Serial.print(noteSwitchInterval);
    Serial.print(",\"pinned\":");
    Serial.print(pinnedNoteIndex);
    Serial.print(",\"notes\":");
    if (LittleFS.exists("/notes.json")) {
      File f = LittleFS.open("/notes.json", "r");
      while (f.available()) Serial.write(f.read());
      f.close();
    } else {
      Serial.print("[]");
    }
    Serial.println("}");
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

  DynamicJsonDocument resDoc(1024);
  resDoc["cmd"] = cmd;
  resDoc["status"] = "ok";

  if (cmd == "sync_time") {
    long epoch = doc["timestamp"].as<long>();
    struct timeval tv;
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
  } else if (cmd == "set_wifi") {
    String newSsid = doc["ssid"].as<String>();
    String newPsk = doc["psk"].as<String>();

    wifi_config_t conf;
    esp_wifi_get_config(WIFI_IF_STA, &conf);
    fallbackSSID = String((char*)conf.sta.ssid);
    fallbackPSK = String((char*)conf.sta.password);

    targetSSID = newSsid;

    WiFi.disconnect();
    delay(100);
    WiFi.begin(newSsid.c_str(), newPsk.c_str());

    isTryingNewWifi = true;
    wifiTryStart = millis();
  } else if (cmd == "wifi_status") {
    resDoc["connected"] = (WiFi.status() == WL_CONNECTED);
    resDoc["ssid"] = WiFi.SSID();
    resDoc["ip"] = WiFi.localIP().toString();
  } else if (cmd == "uptime") {
    resDoc["ms"] = millis();
  } else if (cmd == "get_mode") {
    resDoc["mode"] = displayMode;
  } else if (cmd == "set_mode") {
    int nM = doc["mode"].as<int>();
    if (nM >= 0 && nM <= 2) {
      displayMode = nM;
      saveConfig();
      refreshDisplayByMode();
    } else {
      resDoc["status"] = "error";
    }
  } else if (cmd == "fs_space") {
    size_t t = LittleFS.totalBytes(), u = LittleFS.usedBytes();
    resDoc["total"] = t / 1024;
    resDoc["used"] = u / 1024;
    resDoc["free"] = (t - u) / 1024;
  } else if (cmd == "bright") {
    int v = constrain(doc["v"].as<int>(), 0, 255);
    analogWrite(TFT_BLK, (v * v) / 255);
  } else if (cmd == "format_fs") {
    LittleFS.format();
    currentImageIndex = 0;
    imageCount = 0;
    saveConfig();
    if (displayMode == 0) tft.fillScreen(0);
  } else if (cmd == "reset_system") {
    WiFiManager wm;
    wm.resetSettings();
    Serial.println("{\"cmd\":\"reset_system\",\"status\":\"ok\"}");
    delay(1000);
    ESP.restart();
  } else if (cmd == "image_info") {
    scanImages();
    JsonArray arr = resDoc.createNestedArray("slots");
    for (int i = 0; i < MAX_IMAGES; i++) arr.add(LittleFS.exists("/img" + String(i) + ".bin"));
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
      if (imageCount == 0) tft.fillScreen(ST77XX_BLACK);
      else displayImageFromFile(currentImageIndex);
    }
  } else if (cmd == "set_img_slideshow") {
    if (doc.containsKey("enabled")) slideshowEnabled = doc["enabled"].as<bool>();
    if (doc.containsKey("interval")) switchInterval = constrain(doc["interval"].as<int>(), 3, 300);
    saveConfig();
    if (slideshowEnabled) lastImageSwitch = millis();
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
    if (doc.containsKey("interval")) noteSwitchInterval = constrain(doc["interval"].as<int>(), 3, 300);
    saveConfig();
    if (displayMode == 2) {
      lastNoteSwitch = millis();
      displayNoteOnScreen();
    }
  } else if (cmd == "ota_info") {
    resDoc["current"] = CURRENT_VERSION;
  } else if (cmd == "do_update") {
    Serial.println("{\"cmd\":\"do_update\",\"status\":\"ok\"}");
    delay(500);
    handleDoUpdate();
    return;
  } else if (cmd == "reboot") {
    resDoc["msg"] = "restarting";
    serializeJson(resDoc, Serial);
    Serial.println();
    delay(200);
    ESP.restart();
    return;
  }

  serializeJson(resDoc, Serial);
  Serial.println();
}

