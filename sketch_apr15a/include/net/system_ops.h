#pragma once

#include <WiFiManager.h>

void handleResetSystem();

/** 设置菜单「配网设置」：非阻塞配网热点（开放热点，无密码） */
void startWifiConfigPortalFromSettings();

/** 主循环调用：驱动非阻塞配网 */
void serviceWifiConfigPortal();

bool isWifiConfigPortalActive();

/** 短按退出配网并恢复本机 HTTP 服务 */
void wifiConfigPortalStopFromUser();

/** 配网结束（成功/超时）后需刷新设置菜单时返回一次 true */
bool wifiConfigPortalConsumeRedrawFlag();

/** 配网成功并已连上 WiFi 时返回一次 true，用于全屏提示并退出设置 */
bool wifiConfigPortalConsumeSuccessExit();

/** 清除 WiFiManager 与已保存的 STA 凭据。必须在栈上避免再 new/构造 WiFiManager，防止 C3 栈溢出导致死机。 */
void factoryResetWifiCredentials();
