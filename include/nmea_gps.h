#if NMEA_ENABLE
#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>

// ---------------- Satellites (GSV + GSA) ----------------
// NMEA GSV gives sky-plot geometry + SNR (signal power-ish):
//  - elevation (0..90°)
//  - azimuth (0..359°), degrees from North, clockwise
//  - SNR (dB-Hz) typically 0..60 (can be empty)
// GSA gives the PRNs actually USED in the current fix.
struct NmeaSatInfo {
  uint16_t prn;       // Satellite ID (PRN/SVID). 0 means unused slot.
  uint8_t  elevDeg;   // 0..90
  uint16_t azDeg;     // 0..359
  int8_t   snrDbHz;   // -1 if missing/unknown, else ~0..60
  bool     used;      // true if present in GSA "used satellites" list
};

// Compact snapshot for web UI
struct NmeaGpsSnapshot {
  bool     valid;            // RMC validity (your existing interpretation)
  double   lat;
  double   lon;
  float    speedKmh;

  uint8_t  satsUsed;         // from GGA
  uint8_t  fixQuality;       // from GGA (0/1/2/4/5...)
  uint8_t  fixType;          // from GSA (1/2/3)
  float    hdop;

  bool     timeValid;
  uint8_t  hour, minute, second;

  uint16_t year;             // 2026 (0 if unknown)
  uint8_t  month, day;

  uint32_t ageMs;            // ms since last parsed sentence (0 if never)

  // ---- NEW: satellites in view (from GSV), with "used" from GSA ----
  static constexpr uint8_t MAX_SATS = 48;
  uint8_t    satsInView;
  NmeaSatInfo sats[MAX_SATS];
};

void nmea_begin();
void nmea_feed_bytes(const uint8_t* data, size_t len, uint32_t nowMs);
bool nmea_get_snapshot(NmeaGpsSnapshot& out);
#endif
