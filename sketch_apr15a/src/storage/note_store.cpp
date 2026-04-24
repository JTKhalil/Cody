#include "include/globals.h"
#include "include/storage/note_store.h"
#include "include/core/config_store.h"
#include "include/render/display_render.h"

#if !CODY_ENABLE_WIFI_DEBUG
void handleNoteConfig() {}
void handleSetNoteConfig() {}
void handleGetNotes() {}
void handleSaveNote() {}
void handleDeleteNote() {}

#else
void handleNoteConfig() {
  String json = "{\"pinned\":" + String(pinnedNoteIndex) +
                ",\"slideshow\":" + String(noteSlideshowEnabled ? "true" : "false") +
                ",\"interval\":" + String(noteSwitchInterval) + "}";
  server.send(200, "application/json", json);
}

void handleSetNoteConfig() {
  if (server.hasArg("pinned")) pinnedNoteIndex = server.arg("pinned").toInt();
  if (server.hasArg("slideshow")) noteSlideshowEnabled = (server.arg("slideshow") == "true");
  if (server.hasArg("interval")) noteSwitchInterval = constrain(server.arg("interval").toInt(), 3, 60);
  saveConfig();
  if (displayMode == 2) {
    lastNoteSwitch = millis();
    displayNoteOnScreen();
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleGetNotes() {
  if (!LittleFS.exists("/notes.json")) {
    server.send(200, "application/json", "[]");
    return;
  }
  File f = LittleFS.open("/notes.json", "r");
  server.streamFile(f, "application/json");
  f.close();
}

void handleSaveNote() {
  StaticJsonDocument<4096> doc;
  if (LittleFS.exists("/notes.json")) {
    File f = LittleFS.open("/notes.json", "r");
    deserializeJson(doc, f);
    f.close();
  } else {
    doc.to<JsonArray>();
  }

  String content = server.arg("content");
  if (content.length() > 500) content = content.substring(0, 500);

  struct tm timeinfo;
  char timeStr[20];
  if (getLocalTime(&timeinfo)) strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M", &timeinfo);
  else strcpy(timeStr, "Unknown");

  int editIndex = server.hasArg("index") ? server.arg("index").toInt() : -1;
  if (editIndex >= 0 && editIndex < static_cast<int>(doc.size())) {
    doc[editIndex]["content"] = content;
    doc[editIndex]["time"] = timeStr;
  } else {
    if (doc.size() >= MAX_NOTES) {
      server.send(400, "application/json", "{\"status\":\"error\",\"msg\":\"Full\"}");
      return;
    }
    JsonObject obj = doc.createNestedObject();
    obj["content"] = content;
    obj["time"] = timeStr;
  }

  File f = LittleFS.open("/notes.json", "w");
  serializeJson(doc, f);
  f.close();
  if (displayMode == 2) displayNoteOnScreen();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleDeleteNote() {
  if (!server.hasArg("index")) {
    server.send(400, "text/plain", "No Index");
    return;
  }

  int index = server.arg("index").toInt();
  StaticJsonDocument<4096> doc;
  File f = LittleFS.open("/notes.json", "r");
  deserializeJson(doc, f);
  f.close();

  doc.remove(index);
  if (pinnedNoteIndex == index) {
    pinnedNoteIndex = -1;
    saveConfig();
  } else if (pinnedNoteIndex > index) {
    pinnedNoteIndex--;
    saveConfig();
  }

  f = LittleFS.open("/notes.json", "w");
  serializeJson(doc, f);
  f.close();

  if (displayMode == 2) displayNoteOnScreen();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

#endif

