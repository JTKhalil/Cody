#pragma once

/**
 * 为了让 wxcody-ble 固件在 4MB Flash 的小分区方案（如 min_spiffs）下也能烧录，
 * 默认关闭 WiFi/Web 调试相关功能（HTTP Server / WiFiManager 配网门户 / HTTPUpdate 等）。
 *
 * 如需临时打开（仅调试用），将其改为 1 并重新编译。
 */
#ifndef CODY_ENABLE_WIFI_DEBUG
#define CODY_ENABLE_WIFI_DEBUG 0
#endif

