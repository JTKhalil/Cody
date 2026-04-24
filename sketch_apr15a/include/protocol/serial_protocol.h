#pragma once

#include <Arduino.h>

// Optional protocol line emitter hook.
// The serial protocol always prints JSONL to Serial; when a line emitter is set,
// the same JSON line (without trailing '\n') is also forwarded to the emitter.
using ProtocolLineEmitter = void (*)(const String& line);

void serial_protocol_set_line_emitter(ProtocolLineEmitter emitter);
void serial_protocol_emit_line(const String& line);

void processSerialCommand(const String& payload);

/// 串口切换 WiFi 成功/失败后推送一行 JSON，供 CodyDesk 等待结果（与主循环 isTryingNewWifi 配对）。
void emitWifiJoinResultEvent(bool ok);

