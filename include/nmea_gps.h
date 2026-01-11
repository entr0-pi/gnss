#if NMEA_ENABLE
#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>

// Compact snapshot for web UI
struct NmeaGpsSnapshot {
  bool     valid;            // RMC valid (A) -> true
  double   lat;
  double   lon;
  float    speedKmh;

  uint8_t  satsUsed;         // GGA satellites tracked
  uint8_t  fixQuality;       // GGA fix quality (0/1/2/4/5...)
  uint8_t  fixType;          // GSA fix type (1/2/3) if present
  float    hdop;

  float    hAcc_m;           // horizontal accuracy estimate (m)
  float    vAcc_m;           // vertical accuracy estimate (m)
  uint8_t  accSource;        // 0=none, 1=GST, 2=HDOP-est

  bool     timeValid;
  uint8_t  hour, minute, second;

  uint16_t year;             // 2026 (0 if unknown)
  uint8_t  month, day;

  uint32_t ageMs;            // ms since last parsed sentence (0 if never)
};

void nmea_begin();
void nmea_feed_bytes(const uint8_t* data, size_t len, uint32_t nowMs);
bool nmea_get_snapshot(NmeaGpsSnapshot& out);
#endif