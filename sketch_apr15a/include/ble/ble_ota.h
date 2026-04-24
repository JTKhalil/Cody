#pragma once

#include <stdint.h>

namespace ble_ota {

void init();
void loop();

// Returns true if an OTA session is active.
bool is_active();

// Frame handler entry (called by BLE frame dispatcher).
// Returns true if the frame was consumed (ACK sent, etc).
bool on_frame(uint8_t type, uint8_t session, uint8_t seq, const uint8_t* payload, uint16_t len);

}  // namespace ble_ota

