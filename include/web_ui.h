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

// ---- GPS snapshot interface (implemented in main.cpp) ----
#if NMEA_ENABLE
struct WebuiGpsSnapshot {
  bool     valid;
  double   lat;
  double   lon;
  float    speedKmh;

  uint8_t  satsUsed;
  uint8_t  fixQuality;
  uint8_t  fixType;
  float    hdop;

  bool     timeValid;
  uint8_t  hour, minute, second;

  uint16_t year;
  uint8_t  month, day;

  uint32_t ageMs;

  // ---- NEW: satellites in view for skyplot ----
  uint8_t  satsInView;

  struct Sat {
    uint16_t prn;
    uint8_t  elevDeg;
    uint16_t azDeg;
    int8_t   snrDbHz;
    bool     used;
  };

  static constexpr uint8_t MAX_SATS = 48;
  Sat sats[MAX_SATS];
};
#endif


// Returns true and fills `out` with a snapshot of BLE and GPS status.
bool webui_get_ble_snapshot(WebuiBleSnapshot& out);
#if NMEA_ENABLE
bool webui_get_gps_snapshot(WebuiGpsSnapshot& out);
#endif

