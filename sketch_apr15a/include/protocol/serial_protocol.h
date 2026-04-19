#pragma once

void processSerialCommand(const String& payload);

/// 串口切换 WiFi 成功/失败后推送一行 JSON，供 CodyDesk 等待结果（与主循环 isTryingNewWifi 配对）。
void emitWifiJoinResultEvent(bool ok);

