#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace ble_proto {

// Binary frame:
//   MAGIC(2) TYPE(1) SESSION(1) SEQ(1) LEN(2) PAYLOAD(LEN) CRC16(2)
// - LEN/CRC16 are little-endian.
// - CRC16 is CCITT-FALSE over all bytes from MAGIC through end of PAYLOAD.
static constexpr uint8_t kMagic0 = 0xC0;
static constexpr uint8_t kMagic1 = 0xDE;

enum class FrameType : uint8_t {
  kPing = 0x01,
  kAck = 0x7F,
  // Image gallery transfer (LittleFS RGB565 240x240).
  kImgPullBegin = 0x10,
  kImgPullChunk = 0x11,
  kImgPullFinish = 0x12,
  kImgPushBegin = 0x13,
  kImgPushChunk = 0x14,
  kImgPushFinish = 0x15,
  // OTA firmware update (raw firmware.bin over BLE).
  kOtaBegin = 0x20,
  kOtaChunk = 0x21,
  kOtaFinish = 0x22,
  // Optional status/progress.
  kOtaStatus = 0x23,
  // Handdraw: one segment (RGB565 stroke). Payload 7B: x0,y0,x1,y1 (u8), color (le16), width (u8).
  // Fire-and-forget: no ACK, to minimize airtime/latency.
  kHanddrawStroke = 0x30,
  // 多段手绘：payload = n(1B) + n×7B 线段（与 kHanddrawStroke 单段格式相同）
  kHanddrawStrokeBatch = 0x31,
};

// ACK payload format:
//   origType(1), errCode(1)
// errCode==0 means success.
static constexpr uint8_t kAckErrOk = 0;
static constexpr uint8_t kAckErrBadArg = 1;
static constexpr uint8_t kAckErrFs = 2;
static constexpr uint8_t kAckErrSize = 3;
static constexpr uint8_t kAckErrProto = 4;
static constexpr uint8_t kAckErrBusy = 5;
static constexpr uint8_t kAckErrUpdate = 6;
static constexpr uint8_t kAckErrAuth = 7;

using TxNotifyFn = void (*)(const uint8_t* data, size_t len);
using RxFrameHandler = void (*)(uint8_t type, uint8_t session, uint8_t seq, const uint8_t* payload, uint16_t len);

void init(TxNotifyFn txNotify);
void set_rx_handler(RxFrameHandler handler);

// Send a binary frame (raw).
void tx_frame(uint8_t type, uint8_t session, uint8_t seq, const uint8_t* payload, uint16_t len);

// Helper: send ACK(origType, errCode).
void tx_ack(uint8_t session, uint8_t seq, uint8_t origType, uint8_t errCode);

// Consume bytes from RX buffer (vector owned by caller).
// The function will remove processed bytes from the front of the buffer.
void rx_consume(std::vector<uint8_t>& rxBuf);

// Compute CRC16-CCITT-FALSE.
uint16_t crc16_ccitt_false(const uint8_t* data, size_t len);

}  // namespace ble_proto

