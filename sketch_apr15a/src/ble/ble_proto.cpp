#include "include/ble/ble_proto.h"

#include <vector>

namespace ble_proto {
namespace {

static TxNotifyFn g_txNotify = nullptr;
static RxFrameHandler g_rxHandler = nullptr;

static inline uint16_t read_le16(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline void write_le16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void tx_frame_impl(uint8_t type, uint8_t session, uint8_t seq, const uint8_t* payload, uint16_t len) {
  if (!g_txNotify) return;

  // Frame bytes: 2 + 1 + 1 + 1 + 2 + len + 2
  const size_t total = (size_t)2 + 1 + 1 + 1 + 2 + (size_t)len + 2;
  // BLE notify payload is limited; keep frames small and avoid heap allocation.
  static constexpr size_t kMaxFrameBytes = 180;
  if (total > kMaxFrameBytes) return;
  uint8_t out[kMaxFrameBytes];

  size_t i = 0;
  out[i++] = kMagic0;
  out[i++] = kMagic1;
  out[i++] = type;
  out[i++] = session;
  out[i++] = seq;
  write_le16(&out[i], len);
  i += 2;
  if (len && payload) {
    memcpy(&out[i], payload, len);
  }
  i += len;

  const uint16_t crc = crc16_ccitt_false(out, i);
  write_le16(&out[i], crc);
  i += 2;

  g_txNotify(out, total);
}

static void tx_ack_impl(uint8_t session, uint8_t seq, uint8_t origType, uint8_t errCode) {
  const uint8_t ackPayload[2] = {origType, errCode};
  tx_frame_impl((uint8_t)FrameType::kAck, session, seq, ackPayload, (uint16_t)sizeof(ackPayload));
}

static void handle_frame(uint8_t type, uint8_t session, uint8_t seq, const uint8_t* payload, uint16_t len) {
  (void)payload;
  (void)len;

  if (type == (uint8_t)FrameType::kPing) {
    // Reply immediately with ACK(PING, ok).
    tx_ack_impl(session, seq, (uint8_t)FrameType::kPing, kAckErrOk);
    return;
  }

  if (g_rxHandler) {
    g_rxHandler(type, session, seq, payload, len);
    return;
  }
}

}  // namespace

void init(TxNotifyFn txNotify) {
  g_txNotify = txNotify;
}

void set_rx_handler(RxFrameHandler handler) {
  g_rxHandler = handler;
}

void tx_frame(uint8_t type, uint8_t session, uint8_t seq, const uint8_t* payload, uint16_t len) {
  tx_frame_impl(type, session, seq, payload, len);
}

void tx_ack(uint8_t session, uint8_t seq, uint8_t origType, uint8_t errCode) {
  tx_ack_impl(session, seq, origType, errCode);
}

uint16_t crc16_ccitt_false(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; b++) {
      if (crc & 0x8000) {
        crc = (uint16_t)((crc << 1) ^ 0x1021);
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

void rx_consume(std::vector<uint8_t>& rxBuf) {
  // Keep parsing frames from the front.
  while (true) {
    if (rxBuf.size() < 2) return;
    if (rxBuf[0] != kMagic0 || rxBuf[1] != kMagic1) return;

    // Need at least fixed header after magic: TYPE+SESSION+SEQ+LEN(2) => 5 bytes
    if (rxBuf.size() < 2 + 5) return;

    const uint8_t type = rxBuf[2];
    const uint8_t session = rxBuf[3];
    const uint8_t seq = rxBuf[4];
    const uint16_t len = read_le16(&rxBuf[5]);

    // Full frame size includes payload and CRC16(2).
    const size_t frameSize = (size_t)2 + 1 + 1 + 1 + 2 + (size_t)len + 2;
    if (rxBuf.size() < frameSize) return;

    // CRC check.
    const uint16_t gotCrc = read_le16(&rxBuf[frameSize - 2]);
    const uint16_t calcCrc = crc16_ccitt_false(rxBuf.data(), frameSize - 2);
    if (gotCrc != calcCrc) {
      // Bad frame: drop the first byte to resync (caller may also drop until MAGIC).
      rxBuf.erase(rxBuf.begin());
      continue;
    }

    const uint8_t* payload = (len ? &rxBuf[2 + 1 + 1 + 1 + 2] : nullptr);
    handle_frame(type, session, seq, payload, len);

    // Consume frame.
    rxBuf.erase(rxBuf.begin(), rxBuf.begin() + (ptrdiff_t)frameSize);
  }
}

}  // namespace ble_proto

