#include "include/ble/ble_ota.h"

#include <Arduino.h>
#include <Update.h>

#include "include/ble/ble_proto.h"
#include "include/ble/cody_ble.h"

namespace ble_ota {
namespace {

// BLE notify/frame total limit is ~180B; our frame overhead is 9 bytes (magic+type+session+seq+len+crc)
// OTA chunk payload includes 4B offset, so a safe max is 180 - 9 - 4 = 167.
static constexpr uint16_t kMaxChunkBytes = 166;

static inline uint32_t read_le32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void write_le32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

struct OtaState {
  bool active = false;
  uint8_t session = 0;
  uint32_t totalLen = 0;
  uint32_t expectedOffset = 0;
  uint32_t lastProgressSent = 0;
  uint32_t lastRxMs = 0;
  bool rebootPending = false;
  uint32_t rebootDueMs = 0;
};

static OtaState g_ota;

static void reset_ota(bool abortUpdate) {
  if (abortUpdate) {
    if (Update.isRunning()) {
      Update.abort();
    }
  }
  g_ota = OtaState{};
}

static void tx_status(uint8_t session, uint8_t seq, uint32_t progress, uint8_t errCode) {
  uint8_t pl[4 + 1];
  write_le32(&pl[0], progress);
  pl[4] = errCode;
  ble_proto::tx_frame((uint8_t)ble_proto::FrameType::kOtaStatus, session, seq, pl, (uint16_t)sizeof(pl));
}

static uint8_t update_err_code_or(uint8_t fallback) {
  const int e = (int)Update.getError();
  if (e != 0) {
    // Expose Update error as a small numeric code via status; ACK uses kAckErrUpdate.
    // Keep it in 1 byte for the first iteration.
    return (uint8_t)(e & 0xFF);
  }
  return fallback;
}

static uint8_t handle_ota_begin(uint8_t session, uint8_t seq, const uint8_t* payload, uint16_t len) {
  if (len < 4) return ble_proto::kAckErrProto;
  const uint32_t totalLen = read_le32(payload);
  if (totalLen == 0) return ble_proto::kAckErrBadArg;

  // If an older OTA session got stuck (e.g. client aborted mid-transfer),
  // allow a new OTA_BEGIN to abort and restart instead of reporting BUSY forever.
  if (g_ota.active) {
    reset_ota(true);
  }
  if (!cody_ble_is_connected()) return ble_proto::kAckErrProto;

  Update.abort();  // ensure clean state if previous session crashed
  if (!Update.begin(totalLen)) {
    reset_ota(true);
    tx_status(session, (uint8_t)(seq + 1), 0, update_err_code_or(ble_proto::kAckErrUpdate));
    return ble_proto::kAckErrUpdate;
  }

  g_ota.active = true;
  g_ota.session = session;
  g_ota.totalLen = totalLen;
  g_ota.expectedOffset = 0;
  g_ota.lastProgressSent = 0;
  g_ota.lastRxMs = millis();

  return ble_proto::kAckErrOk;
}

static uint8_t handle_ota_chunk(uint8_t session, uint8_t seq, const uint8_t* payload, uint16_t len) {
  if (!g_ota.active) return ble_proto::kAckErrProto;
  if (session != g_ota.session) return ble_proto::kAckErrProto;
  if (len < 4) return ble_proto::kAckErrProto;

  g_ota.lastRxMs = millis();
  const uint32_t off = read_le32(payload);
  const uint16_t n = (uint16_t)(len - 4);
  if (n == 0) return ble_proto::kAckErrBadArg;
  if (n > kMaxChunkBytes) return ble_proto::kAckErrSize;
  if (off != g_ota.expectedOffset) return ble_proto::kAckErrProto;
  if (off + (uint32_t)n > g_ota.totalLen) return ble_proto::kAckErrSize;

  if (!Update.isRunning()) return ble_proto::kAckErrProto;
  const size_t wrote = Update.write((uint8_t*)(payload + 4), n);
  if (wrote != n) {
    reset_ota(true);
    tx_status(session, (uint8_t)(seq + 1), g_ota.expectedOffset, update_err_code_or(ble_proto::kAckErrUpdate));
    return ble_proto::kAckErrUpdate;
  }

  g_ota.expectedOffset += (uint32_t)n;

  // Optional progress (throttle to reduce notify spam).
  const uint32_t prog = g_ota.expectedOffset;
  const uint32_t step = 4096;
  if (prog == g_ota.totalLen || (prog / step) != (g_ota.lastProgressSent / step)) {
    g_ota.lastProgressSent = prog;
    tx_status(session, (uint8_t)(seq + 1), prog, ble_proto::kAckErrOk);
  }

  return ble_proto::kAckErrOk;
}

static uint8_t handle_ota_finish(uint8_t session, uint8_t seq, const uint8_t* payload, uint16_t len) {
  if (!g_ota.active) return ble_proto::kAckErrProto;
  if (session != g_ota.session) return ble_proto::kAckErrProto;
  if (len < 4) return ble_proto::kAckErrProto;

  const uint32_t totalLen = read_le32(payload);
  if (totalLen != g_ota.totalLen) return ble_proto::kAckErrSize;
  if (g_ota.expectedOffset != g_ota.totalLen) return ble_proto::kAckErrProto;

  if (!Update.isRunning()) {
    reset_ota(true);
    return ble_proto::kAckErrProto;
  }

  const bool ok = Update.end(false /* evenIfRemaining */);
  if (!ok) {
    reset_ota(true);
    tx_status(session, (uint8_t)(seq + 1), g_ota.expectedOffset, update_err_code_or(ble_proto::kAckErrUpdate));
    return ble_proto::kAckErrUpdate;
  }

  // Success: keep OTA state until restart.
  tx_status(session, (uint8_t)(seq + 1), g_ota.totalLen, ble_proto::kAckErrOk);
  // Restart is triggered in loop() after ACK/status has had time to flush.
  g_ota.rebootPending = true;
  g_ota.rebootDueMs = millis() + 1500;
  return ble_proto::kAckErrOk;
}

}  // namespace

void init() {
  reset_ota(true);
}

void loop() {
  if (!cody_ble_is_connected()) {
    if (g_ota.active) reset_ota(true);
    return;
  }

  // Watchdog: abort stuck OTA if no chunks arrive for a while.
  if (g_ota.active && !g_ota.rebootPending) {
    const uint32_t now = millis();
    if (g_ota.lastRxMs && (int32_t)(now - g_ota.lastRxMs) > 15000) {
      reset_ota(true);
    }
  }

  if (g_ota.rebootPending) {
    const uint32_t now = millis();
    if ((int32_t)(now - g_ota.rebootDueMs) >= 0) {
      delay(50);
      ESP.restart();
    }
  }
}

bool is_active() {
  return g_ota.active;
}

bool on_frame(uint8_t type, uint8_t session, uint8_t seq, const uint8_t* payload, uint16_t len) {
  if (type == (uint8_t)ble_proto::FrameType::kOtaBegin) {
    const uint8_t err = handle_ota_begin(session, seq, payload, len);
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kOtaBegin, err);
    return true;
  }
  if (type == (uint8_t)ble_proto::FrameType::kOtaChunk) {
    const uint8_t err = handle_ota_chunk(session, seq, payload, len);
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kOtaChunk, err);
    return true;
  }
  if (type == (uint8_t)ble_proto::FrameType::kOtaFinish) {
    const uint8_t err = handle_ota_finish(session, seq, payload, len);
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kOtaFinish, err);
    if (err != ble_proto::kAckErrOk) {
      reset_ota(true);
    }
    return true;
  }
  return false;
}

}  // namespace ble_ota

