#pragma once

#include <Arduino.h>

// Minimal BLE GATT skeleton:
// - Advertises with name prefix "Cody-"
// - Service: 0000C0DE-0000-1000-8000-00805F9B34FB
// - RX (Write/WriteNR): 0000C0D1-0000-1000-8000-00805F9B34FB
// - TX (Notify): 0000C0D2-0000-1000-8000-00805F9B34FB

void cody_ble_init();
void cody_ble_loop();

bool cody_ble_is_connected();

// For on-device UI display (settings -> 蓝牙信息).
const char* cody_ble_get_name();
const char* cody_ble_get_password();

// Authorization: require password before handling commands/frames.
bool cody_ble_is_authed();

// Pairing confirmation (first-time connect): pending until user confirms on device.
bool cody_ble_pair_pending();
const char* cody_ble_pair_peer();
void cody_ble_pair_accept();
void cody_ble_pair_reject();

// Clear paired/trusted records (forces re-pair next time).
void cody_ble_clear_trusted();

// Single-binding helpers (for on-device UI).
bool cody_ble_has_trusted();
const char* cody_ble_get_trusted_name();
void cody_ble_clear_trusted_and_disconnect();

