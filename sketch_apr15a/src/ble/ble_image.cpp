#include "include/ble/ble_image.h"

#include <LittleFS.h>

#include "include/globals.h"
#include "include/ble/ble_ota.h"
#include "include/ble/ble_proto.h"
#include "include/ble/cody_ble.h"
#include "include/storage/image_store.h"
#include "include/core/config_store.h"

namespace ble_image {
namespace {

static constexpr uint32_t kImageBytes = 240u * 240u * 2u;  // RGB565
// Frame total limit is ~180B; our frame overhead is 9 bytes and payload has 1(slot)+4(off)+data.
// Safe max: 180 - 9 - 5 = 166.
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

static bool is_valid_slot(uint8_t slot) {
  // slot id is a uint8; keep within a safe numeric range for filenames.
  return slot <= 250;
}

static String slot_path(uint8_t slot) {
  return String("/img") + String((int)slot) + String(".bin");
}

struct PullState {
  bool active = false;
  uint8_t slot = 0;
  uint8_t session = 0;
  uint8_t nextSeq = 0;
  uint32_t offset = 0;
  File f;

  bool waitingAck = false;
  uint8_t waitingSeq = 0;
  uint8_t waitingType = 0;
};

struct PushState {
  bool active = false;
  uint8_t slot = 0;
  uint32_t totalLen = 0;
  uint32_t expectedOffset = 0;
  File f;
};

static PullState g_pull;
static PushState g_push;

static void reset_pull() {
  if (g_pull.f) g_pull.f.close();
  g_pull = PullState{};
}

static void reset_push(bool removeTmp) {
  if (g_push.f) g_push.f.close();
  g_push = PushState{};
  if (removeTmp) {
    if (LittleFS.exists("/tmp_ble_img.bin")) LittleFS.remove("/tmp_ble_img.bin");
  }
}

static void reset_all() {
  reset_pull();
  reset_push(false);
}

static void pull_send_next_chunk() {
  if (!g_pull.active) return;
  if (g_pull.waitingAck) return;
  if (!g_pull.f) {
    reset_pull();
    return;
  }
  if (g_pull.offset >= kImageBytes) {
    // Send FINISH(slot,totalLen) and wait ACK (optional but keeps stop-and-wait symmetry).
    uint8_t pl[1 + 4];
    pl[0] = g_pull.slot;
    write_le32(&pl[1], (uint32_t)kImageBytes);
    const uint8_t seq = g_pull.nextSeq++;
    ble_proto::tx_frame((uint8_t)ble_proto::FrameType::kImgPullFinish, g_pull.session, seq, pl, (uint16_t)sizeof(pl));
    g_pull.waitingAck = true;
    g_pull.waitingSeq = seq;
    g_pull.waitingType = (uint8_t)ble_proto::FrameType::kImgPullFinish;
    // Finish is the last message; we keep state until ACK or disconnect.
    return;
  }

  uint8_t buf[kMaxChunkBytes];
  const uint16_t want = (uint16_t)min<uint32_t>((uint32_t)kMaxChunkBytes, kImageBytes - g_pull.offset);
  const int readN = g_pull.f.read(buf, want);
  if (readN <= 0) {
    reset_pull();
    return;
  }

  uint8_t pl[1 + 4 + kMaxChunkBytes];
  pl[0] = g_pull.slot;
  write_le32(&pl[1], g_pull.offset);
  memcpy(&pl[5], buf, (size_t)readN);

  const uint8_t seq = g_pull.nextSeq++;
  ble_proto::tx_frame((uint8_t)ble_proto::FrameType::kImgPullChunk, g_pull.session, seq, pl, (uint16_t)(5 + readN));
  g_pull.waitingAck = true;
  g_pull.waitingSeq = seq;
  g_pull.waitingType = (uint8_t)ble_proto::FrameType::kImgPullChunk;
  g_pull.offset += (uint32_t)readN;
}

static void start_pull(uint8_t slot, uint8_t session) {
  reset_pull();
  g_pull.active = true;
  g_pull.slot = slot;
  g_pull.session = session;
  g_pull.nextSeq = 1;
  g_pull.offset = 0;
  g_pull.waitingAck = false;
  g_pull.f = LittleFS.open(slot_path(slot), "r");
}

static void handle_ack(uint8_t session, uint8_t seq, const uint8_t* payload, uint16_t len) {
  if (len < 2) return;
  const uint8_t origType = payload[0];
  const uint8_t errCode = payload[1];

  if (!g_pull.active) return;
  if (session != g_pull.session) return;
  if (!g_pull.waitingAck) return;
  if (seq != g_pull.waitingSeq) return;
  if (origType != g_pull.waitingType) return;
  if (errCode != ble_proto::kAckErrOk) {
    reset_pull();
    return;
  }

  g_pull.waitingAck = false;
  if (origType == (uint8_t)ble_proto::FrameType::kImgPullFinish) {
    reset_pull();
    return;
  }
}

static void handle_img_pull_begin(uint8_t session, uint8_t seq, const uint8_t* payload, uint16_t len) {
  if (len < 1) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPullBegin, ble_proto::kAckErrProto);
    return;
  }
  const uint8_t slot = payload[0];
  if (!is_valid_slot(slot)) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPullBegin, ble_proto::kAckErrBadArg);
    return;
  }
  if (g_push.active || g_pull.active) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPullBegin, ble_proto::kAckErrBusy);
    return;
  }

  const String path = slot_path(slot);
  if (!LittleFS.exists(path)) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPullBegin, ble_proto::kAckErrFs);
    return;
  }
  File f = LittleFS.open(path, "r");
  if (!f) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPullBegin, ble_proto::kAckErrFs);
    return;
  }
  const size_t sz = f.size();
  f.close();
  if (sz != kImageBytes) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPullBegin, ble_proto::kAckErrSize);
    return;
  }

  ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPullBegin, ble_proto::kAckErrOk);
  start_pull(slot, session);
}

static void handle_img_push_begin(uint8_t session, uint8_t seq, const uint8_t* payload, uint16_t len) {
  if (len < 1 + 4) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushBegin, ble_proto::kAckErrProto);
    return;
  }
  const uint8_t slot = payload[0];
  const uint32_t totalLen = read_le32(&payload[1]);

  if (!is_valid_slot(slot)) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushBegin, ble_proto::kAckErrBadArg);
    return;
  }
  if (totalLen != kImageBytes) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushBegin, ble_proto::kAckErrSize);
    return;
  }
  if (g_push.active || g_pull.active) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushBegin, ble_proto::kAckErrBusy);
    return;
  }

  if (LittleFS.exists("/tmp_ble_img.bin")) LittleFS.remove("/tmp_ble_img.bin");
  File f = LittleFS.open("/tmp_ble_img.bin", "w");
  if (!f) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushBegin, ble_proto::kAckErrFs);
    return;
  }

  g_push.active = true;
  g_push.slot = slot;
  g_push.totalLen = totalLen;
  g_push.expectedOffset = 0;
  g_push.f = f;

  ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushBegin, ble_proto::kAckErrOk);
}

static void handle_img_push_chunk(uint8_t session, uint8_t seq, const uint8_t* payload, uint16_t len) {
  if (!g_push.active) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushChunk, ble_proto::kAckErrProto);
    return;
  }
  if (len < 1 + 4) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushChunk, ble_proto::kAckErrProto);
    return;
  }
  const uint8_t slot = payload[0];
  const uint32_t off = read_le32(&payload[1]);
  const uint16_t n = (uint16_t)(len - 5);

  if (slot != g_push.slot) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushChunk, ble_proto::kAckErrBadArg);
    return;
  }
  if (off != g_push.expectedOffset) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushChunk, ble_proto::kAckErrProto);
    return;
  }
  if ((uint32_t)off + (uint32_t)n > g_push.totalLen) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushChunk, ble_proto::kAckErrSize);
    return;
  }
  if (!g_push.f) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushChunk, ble_proto::kAckErrFs);
    return;
  }

  const size_t wrote = g_push.f.write(&payload[5], n);
  if (wrote != n) {
    reset_push(true);
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushChunk, ble_proto::kAckErrFs);
    return;
  }
  g_push.expectedOffset += (uint32_t)n;
  ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushChunk, ble_proto::kAckErrOk);
}

static void handle_img_push_finish(uint8_t session, uint8_t seq, const uint8_t* payload, uint16_t len) {
  if (!g_push.active) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushFinish, ble_proto::kAckErrProto);
    return;
  }
  if (len < 1 + 4) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushFinish, ble_proto::kAckErrProto);
    return;
  }
  const uint8_t slot = payload[0];
  const uint32_t totalLen = read_le32(&payload[1]);

  if (slot != g_push.slot) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushFinish, ble_proto::kAckErrBadArg);
    return;
  }
  if (totalLen != g_push.totalLen) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushFinish, ble_proto::kAckErrSize);
    return;
  }
  if (g_push.expectedOffset != g_push.totalLen) {
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushFinish, ble_proto::kAckErrProto);
    return;
  }

  if (g_push.f) g_push.f.close();

  const String dst = slot_path(g_push.slot);
  if (LittleFS.exists(dst)) LittleFS.remove(dst);
  if (!LittleFS.rename("/tmp_ble_img.bin", dst)) {
    reset_push(true);
    ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushFinish, ble_proto::kAckErrFs);
    return;
  }

  // Refresh gallery state.
  scanImages();
  currentImageIndex = (int)g_push.slot;
  saveConfig();
  if (displayMode == 0) {
    if (imageCount == 0) {
      tft.fillScreen(ST77XX_BLACK);
    } else {
      displayImageFromFile(currentImageIndex);
      lastImageSwitch = millis();
    }
  }

  reset_push(false);
  ble_proto::tx_ack(session, seq, (uint8_t)ble_proto::FrameType::kImgPushFinish, ble_proto::kAckErrOk);
}

static void on_frame(uint8_t type, uint8_t session, uint8_t seq, const uint8_t* payload, uint16_t len) {
  // 密码鉴权已移除；在配对确认未通过前，阻止所有二进制请求。
  if (cody_ble_pair_pending()) {
    if (type != (uint8_t)ble_proto::FrameType::kAck) {
      ble_proto::tx_ack(session, seq, type, ble_proto::kAckErrBusy);
    }
    return;
  }

  // Dispatch OTA frames first (keeps BLE RX handler single-owner).
  if (ble_ota::on_frame(type, session, seq, payload, len)) {
    return;
  }

  if (type == (uint8_t)ble_proto::FrameType::kAck) {
    handle_ack(session, seq, payload, len);
    return;
  }

  if (type == (uint8_t)ble_proto::FrameType::kImgPullBegin) {
    handle_img_pull_begin(session, seq, payload, len);
    return;
  }
  if (type == (uint8_t)ble_proto::FrameType::kImgPushBegin) {
    handle_img_push_begin(session, seq, payload, len);
    return;
  }
  if (type == (uint8_t)ble_proto::FrameType::kImgPushChunk) {
    handle_img_push_chunk(session, seq, payload, len);
    return;
  }
  if (type == (uint8_t)ble_proto::FrameType::kImgPushFinish) {
    handle_img_push_finish(session, seq, payload, len);
    return;
  }
}

}  // namespace

void init() {
  ble_proto::set_rx_handler(&on_frame);
}

void loop() {
  if (!cody_ble_is_connected()) {
    reset_all();
    return;
  }
  pull_send_next_chunk();
}

void cancel_push() {
  // Cancel current push and remove temp file to avoid stale BUSY state.
  reset_push(true);
}

}  // namespace ble_image

