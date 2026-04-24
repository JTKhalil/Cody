#include "include/ble/cody_ble.h"

#include <esp_mac.h>
#include <esp_system.h>
#include <vector>

// NOTE: Arduino's library discovery happens before preprocessing; conditional
// includes like __has_include can prevent the builder from adding the library's
// include path. We therefore include NimBLE unconditionally.
#include <NimBLEDevice.h>

#include "include/globals.h"
#include "include/protocol/serial_protocol.h"
#include "include/ble/ble_proto.h"

static constexpr const char* kServiceUuid = "0000C0DE-0000-1000-8000-00805F9B34FB";
static constexpr const char* kRxUuid = "0000C0D1-0000-1000-8000-00805F9B34FB";
static constexpr const char* kTxUuid = "0000C0D2-0000-1000-8000-00805F9B34FB";

static volatile bool g_connected = false;
static volatile bool g_txSubscribed = false;
static volatile bool g_allowed = false;

static NimBLECharacteristic* g_tx = nullptr;
static NimBLEServer* g_server = nullptr;
static uint16_t g_connHandle = 0;

static std::vector<uint8_t> g_rxBuf;

static String g_bleName;
static String g_blePassword;
static String g_peerAddr;
static String g_pairClientName;
static String g_pairClientId;
static bool g_pairPending = false;
static std::vector<String> g_trusted;
static String g_trustedName;

static String randAlnum(size_t len) {
  static const char kChars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  static constexpr size_t kN = sizeof(kChars) - 1;
  if (len < 1) len = 1;
  if (len > 32) len = 32;
  String out;
  out.reserve((unsigned int)len);
  for (size_t i = 0; i < len; i++) {
    const uint32_t r = esp_random();
    out += kChars[r % kN];
  }
  return out;
}

static void loadTrusted() {
  g_trusted.clear();
  const char* path = "/ble_trusted.txt";
  if (!LittleFS.exists(path)) return;
  File f = LittleFS.open(path, "r");
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    bool dup = false;
    for (const auto& s : g_trusted) {
      if (s == line) { dup = true; break; }
    }
    if (!dup) g_trusted.push_back(line);
    if (g_trusted.size() >= 8) break;
  }
  f.close();
}

static void loadTrustedName() {
  g_trustedName = "";
  const char* path = "/ble_trusted_name.txt";
  if (!LittleFS.exists(path)) return;
  File f = LittleFS.open(path, "r");
  if (!f) return;
  String line = f.readStringUntil('\n');
  line.trim();
  f.close();
  if (line.length()) g_trustedName = line;
}

static void saveTrustedName(const String& name) {
  const String n = String(name);
  File f = LittleFS.open("/ble_trusted_name.txt", "w");
  if (!f) return;
  f.print(n);
  f.print("\n");
  f.close();
  g_trustedName = n;
}

static void appendTrusted(const String& addr) {
  if (!addr.length()) return;
  for (const auto& s : g_trusted) {
    if (s == addr) return;
  }
  g_trusted.push_back(addr);
  // Keep file append-only; small list.
  File f = LittleFS.open("/ble_trusted.txt", "a");
  if (f) {
    f.print(addr);
    f.print("\n");
    f.close();
  }
}

static bool isTrusted(const String& addr) {
  if (!addr.length()) return false;
  for (const auto& s : g_trusted) {
    if (s == addr) return true;
  }
  return false;
}

static void clearTrustedInternal() {
  g_trusted.clear();
  LittleFS.remove("/ble_trusted.txt");
  LittleFS.remove("/ble_trusted_name.txt");
  g_trustedName = "";
}

static void loadOrCreateBlePassword() {
  // Keep a stable password across reboots so the user can reconnect reliably.
  // Stored in LittleFS as a single line.
  const char* path = "/ble_pass.txt";
  String pass;
  if (LittleFS.exists(path)) {
    File f = LittleFS.open(path, "r");
    if (f) {
      pass = f.readStringUntil('\n');
      pass.trim();
      f.close();
    }
  }
  if (pass.length() < 6) {
    pass = randAlnum(10);
    File f = LittleFS.open(path, "w");
    if (f) {
      f.print(pass);
      f.print("\n");
      f.close();
    }
  }
  g_blePassword = pass;
}

static void cody_ble_notify_line(const String& line) {
  if (!g_connected) return;
  if (!g_txSubscribed) return;
  if (!g_tx) return;

  // BLE notify payload is limited; we must chunk long JSON lines (e.g. get_notes)
  // instead of truncating them, otherwise the client can't parse the response.
  static constexpr size_t kMaxNotifyLineBytes = 180;
  if (line.length() == 0) return;

  // Send the JSON line body in chunks, then a final '\n' to terminate the JSONL record.
  const char* p = line.c_str();
  size_t remaining = line.length();
  while (remaining > 0) {
    const size_t n = remaining > kMaxNotifyLineBytes ? kMaxNotifyLineBytes : remaining;
    g_tx->setValue((uint8_t*)p, n);
    (void)g_tx->notify();
    p += n;
    remaining -= n;
    yield();
  }
  const char nl = '\n';
  g_tx->setValue((uint8_t*)&nl, 1);
  (void)g_tx->notify();
}

static void cody_ble_notify_raw(const uint8_t* data, size_t len) {
  if (!g_connected) return;
  if (!g_txSubscribed) return;
  if (!g_tx) return;
  if (!data || len == 0) return;

  // Keep frames small; BLE notify payload is limited.
  static constexpr size_t kMaxNotifyBytes = 180;
  if (len > kMaxNotifyBytes) return;

  g_tx->setValue((uint8_t*)data, len);
  (void)g_tx->notify();
}

class CodyServerCallbacks final : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& connInfo) override {
    g_allowed = false;
    g_connHandle = connInfo.getConnHandle();
    g_peerAddr = String(connInfo.getAddress().toString().c_str());
    g_pairClientName = "";
    g_pairClientId = "";

    g_connected = true;
    // 请求更短连接间隔（单位 1.25ms），降低小包写入延迟；最终由手机协议栈接受/协商
    if (s && g_connHandle != BLE_HS_CONN_HANDLE_NONE) {
      s->updateConnParams(g_connHandle, 8, 12, 0, 400);
    }
    // 绑定判定改为 clientId（小程序稳定生成），避免手机 BLE 隐私地址变化导致“以前连过但不再信任”。
    // 是否自动放行将在收到 pair_hello(id) 后决定。
    g_pairPending = true;
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& connInfo, int reason) override {
    (void)s;
    (void)connInfo;
    (void)reason;
    g_connected = false;
    g_txSubscribed = false;
    g_allowed = false;
    g_pairPending = false;
    g_peerAddr = "";
    g_pairClientName = "";
    g_pairClientId = "";
    g_rxBuf.clear();
    NimBLEDevice::startAdvertising();
  }
};

class CodyRxCallbacks final : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    (void)c;
    (void)connInfo;
    const std::string v = c->getValue();
    if (v.empty()) return;

    const size_t oldSize = g_rxBuf.size();
    g_rxBuf.resize(oldSize + v.size());
    memcpy(g_rxBuf.data() + oldSize, v.data(), v.size());

    // Cap the buffer to prevent unbounded growth on malformed input.
    static constexpr size_t kMaxRxBufBytes = 32768;
    if (g_rxBuf.size() > kMaxRxBufBytes) {
      const size_t drop = g_rxBuf.size() - kMaxRxBufBytes;
      g_rxBuf.erase(g_rxBuf.begin(), g_rxBuf.begin() + (ptrdiff_t)drop);
    }
  }
};

class CodyTxCallbacks final : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* c, NimBLEConnInfo& connInfo, uint16_t subValue) override {
    (void)c;
    (void)connInfo;
    g_txSubscribed = (subValue != 0);
  }
};

static void cody_ble_start_advertising(const String& name) {
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();

  // Ensure device name is visible and stable in scan lists.
  // Many scanners display the name only if present in advertising or scan response data.
  NimBLEAdvertisementData advData;
  advData.addTxPower();
  advData.addServiceUUID(kServiceUuid);
  // WeChat / some scanners may not always parse scan response name reliably,
  // so also put the complete name into the primary advertising payload.
  advData.setName(std::string(name.c_str()), true /* complete */);

  NimBLEAdvertisementData scanData;
  scanData.setName(std::string(name.c_str()), true /* complete */);

  adv->setAdvertisementData(advData);
  adv->setScanResponseData(scanData);
  adv->start();
}

static String cody_ble_build_name() {
  // Use BT MAC so each flashed device advertises a distinct name.
  // esp_read_mac is explicit across different ESP targets.
  uint8_t m[6] = {0};
  esp_err_t err = esp_read_mac(m, ESP_MAC_BT);
  if (err != ESP_OK) {
    // Fallback: derive from efuse (lower 48 bits).
    const uint64_t mac = ESP.getEfuseMac();
    m[0] = (uint8_t)((mac >> 40) & 0xFF);
    m[1] = (uint8_t)((mac >> 32) & 0xFF);
    m[2] = (uint8_t)((mac >> 24) & 0xFF);
    m[3] = (uint8_t)((mac >> 16) & 0xFF);
    m[4] = (uint8_t)((mac >> 8) & 0xFF);
    m[5] = (uint8_t)((mac >> 0) & 0xFF);
  }

  char buf[32];
  // Use the last 3 bytes (commonly varying across boards).
  snprintf(buf, sizeof(buf), "Cody-%02X%02X%02X", m[3], m[4], m[5]);
  return String(buf);
}

static String extractJsonStringField(const String& line, const char* key) {
  const String k = String("\"") + key + String("\"");
  int p = line.indexOf(k);
  if (p < 0) return String("");
  p = line.indexOf(':', p + k.length());
  if (p < 0) return String("");
  // Skip spaces
  while (p + 1 < (int)line.length() && (line[p + 1] == ' ' || line[p + 1] == '\t')) p++;
  if (p + 1 >= (int)line.length() || line[p + 1] != '"') return String("");
  int s = p + 2;
  int e = line.indexOf('"', s);
  if (e < 0) return String("");
  return line.substring(s, e);
}

static void emitPairStatus() {
  String out;
  out.reserve(96);
  out += "{\"cmd\":\"pair_status\",\"status\":\"ok\",\"pending\":";
  out += (g_pairPending ? "true" : "false");
  out += "}";
  serial_protocol_emit_line(out);
}

void cody_ble_init() {
  g_bleName = cody_ble_build_name();
  // LittleFS is initialized in setup() before BLE init in this firmware.
  loadOrCreateBlePassword();
  loadTrusted();
  loadTrustedName();

  NimBLEDevice::init(g_bleName.c_str());
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  // 允许更大的 ATT MTU（对端发起交换时可协商到更高值）
  NimBLEDevice::setMTU(247);

  static CodyServerCallbacks s_serverCb;
  static CodyRxCallbacks s_rxCb;
  static CodyTxCallbacks s_txCb;

  g_server = NimBLEDevice::createServer();
  // deleteCallbacks=false because we pass a static callback object.
  g_server->setCallbacks(&s_serverCb, false);

  NimBLEService* svc = g_server->createService(kServiceUuid);

  g_tx = svc->createCharacteristic(kTxUuid, NIMBLE_PROPERTY::NOTIFY);
  g_tx->setCallbacks(&s_txCb);

  NimBLECharacteristic* rx = svc->createCharacteristic(
      kRxUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(&s_rxCb);

  svc->start();
  cody_ble_start_advertising(g_bleName);

  // Bridge protocol-layer JSON lines to BLE TX notify.
  serial_protocol_set_line_emitter(&cody_ble_notify_line);

  // Binary protocol TX (ACK, etc).
  ble_proto::init(&cody_ble_notify_raw);
}

void cody_ble_loop() {
  // Demux RX stream:
  // - If buffer starts with '{' => JSONL (newline delimited).
  // - If buffer starts with MAGIC => binary frame(s).
  // - Otherwise drop bytes until next '{' or MAGIC.
  while (true) {
    if (g_rxBuf.empty()) return;

    const bool isJson = (g_rxBuf[0] == (uint8_t)'{');
    const bool isBin = (g_rxBuf.size() >= 2 && g_rxBuf[0] == ble_proto::kMagic0 && g_rxBuf[1] == ble_proto::kMagic1);

    if (isJson) {
      // Find newline.
      int nl = -1;
      for (size_t i = 0; i < g_rxBuf.size(); i++) {
        if (g_rxBuf[i] == (uint8_t)'\n') {
          nl = (int)i;
          break;
        }
      }
      if (nl < 0) return;  // wait for more

      String line;
      line.reserve((unsigned int)nl);
      for (int i = 0; i < nl; i++) line += (char)g_rxBuf[(size_t)i];
      g_rxBuf.erase(g_rxBuf.begin(), g_rxBuf.begin() + (ptrdiff_t)(nl + 1));

      line.trim();
      if (line.length() == 0) continue;
      if (!line.startsWith("{")) continue;
      const String cmd = extractJsonStringField(line, "cmd");
      if (cmd == "pair_status") {
        emitPairStatus();
        continue;
      }
      if (cmd == "pair_hello") {
        const String name = extractJsonStringField(line, "name");
        const String cid = extractJsonStringField(line, "id");
        if (name.length()) g_pairClientName = name;
        if (cid.length()) g_pairClientId = cid;

        // 已绑定：只允许同一 clientId 自动放行；其它设备直接拒绝断开
        if (!g_trusted.empty() && g_pairClientId.length()) {
          if (isTrusted(g_pairClientId)) {
            g_pairPending = false;
            g_allowed = true;
          } else {
            if (g_server) g_server->disconnect(g_connHandle);
          }
        }
        serial_protocol_emit_line("{\"cmd\":\"pair_hello\",\"status\":\"ok\"}");
        continue;
      }
      if (g_pairPending) {
        String out;
        out.reserve(96);
        out += "{\"cmd\":\"";
        out += (cmd.length() ? cmd : String("unknown"));
        out += "\",\"status\":\"error\",\"msg\":\"need_confirm\"}";
        serial_protocol_emit_line(out);
        continue;
      }
      processSerialCommand(line);
      continue;
    }

    if (isBin) {
      const size_t before = g_rxBuf.size();
      ble_proto::rx_consume(g_rxBuf);
      if (g_rxBuf.size() == before) {
        // Not enough bytes for a full frame yet.
        return;
      }
      continue;
    }

    // Resync: drop until '{' or MAGIC.
    size_t keepFrom = g_rxBuf.size();
    for (size_t i = 0; i < g_rxBuf.size(); i++) {
      if (g_rxBuf[i] == (uint8_t)'{') {
        keepFrom = i;
        break;
      }
      if (i + 1 < g_rxBuf.size() && g_rxBuf[i] == ble_proto::kMagic0 && g_rxBuf[i + 1] == ble_proto::kMagic1) {
        keepFrom = i;
        break;
      }
    }
    if (keepFrom == 0) continue;
    if (keepFrom >= g_rxBuf.size()) {
      g_rxBuf.clear();
      return;
    }
    g_rxBuf.erase(g_rxBuf.begin(), g_rxBuf.begin() + (ptrdiff_t)keepFrom);
  }
}

bool cody_ble_is_connected() {
  return g_connected;
}

bool cody_ble_is_authed() {
  return g_allowed;
}

bool cody_ble_pair_pending() {
  return g_pairPending && g_connected;
}

const char* cody_ble_pair_peer() {
  // 连接确认页仅展示小程序上报的设备名称（用于确认是哪台手机在请求连接）
  if (g_pairClientName.length()) return g_pairClientName.c_str();
  return "";
}

void cody_ble_pair_accept() {
  if (!g_pairPending) return;
  g_pairPending = false;
  g_allowed = true;
  // 单设备绑定：以 clientId 为准（稳定），避免 BLE 地址变化导致失效
  if (g_pairClientId.length()) {
    clearTrustedInternal();
    appendTrusted(g_pairClientId);
    // 同时持久化“信任设备名称”，用于设置页展示
    if (g_pairClientName.length()) saveTrustedName(g_pairClientName);
    else saveTrustedName(String("已绑定设备"));
  }
}

void cody_ble_pair_reject() {
  if (!g_connected) return;
  if (g_server) {
    g_server->disconnect(g_connHandle);
  }
  g_pairPending = false;
  g_allowed = false;
}

void cody_ble_clear_trusted() {
  clearTrustedInternal();
  // If currently connected, force next time to require confirm again.
  g_pairPending = false;
  g_allowed = false;
}

bool cody_ble_has_trusted() {
  return !g_trusted.empty();
}

const char* cody_ble_get_trusted_name() {
  return g_trustedName.c_str();
}

void cody_ble_clear_trusted_and_disconnect() {
  clearTrustedInternal();
  g_pairPending = false;
  g_allowed = false;
  if (g_connected && g_server) {
    g_server->disconnect(g_connHandle);
  }
}

const char* cody_ble_get_name() {
  return g_bleName.c_str();
}

const char* cody_ble_get_password() {
  // 密码鉴权已移除；仍保留用于“蓝牙信息”页展示（可作为人工核对/扩展用途）。
  return g_blePassword.c_str();
}

