// ======================= web_ui.h (FINAL - snapshot method) =======================
#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <IPAddress.h>

void webui_begin(WebServer& server, const IPAddress& sta_dns);

// ---- BLE snapshot interface (implemented in main.cpp) ----
struct WebuiBleSnapshot {
  bool     connected;
  uint16_t mtu;
  uint32_t txBytes;          // truncated from uint64_t
  uint32_t rxBytes;          // truncated from uint64_t
};

// Returns true and fills `out` with a snapshot of BLE status.
bool webui_get_ble_snapshot(WebuiBleSnapshot& out);
