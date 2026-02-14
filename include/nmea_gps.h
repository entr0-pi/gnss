#if NMEA_ENABLE
#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>

// Maximum satellites tracked across all constellations (GPS/GLONASS/Galileo/BeiDou).
// UM980 can report 40+ satellites; 48 gives headroom without wasting RAM.
#ifndef NMEA_MAX_SATS
#define NMEA_MAX_SATS 48
#endif

// Per-satellite information extracted from GSV sentences.
struct NmeaSatInfo {
  int16_t  nr;            // satellite PRN number
  int16_t  elevation;     // degrees above horizon (0-90)
  int16_t  azimuth;       // degrees from true north (0-359)
  int16_t  snr;           // signal-to-noise ratio (dBHz), 0 = not tracked
  uint8_t  constellation; // 0=GPS, 1=GLONASS, 2=Galileo, 3=BeiDou, 4=other
};

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

  // Satellite details from GSV sentences
  uint8_t     satCount;        // number of valid entries in sats[]
  NmeaSatInfo sats[NMEA_MAX_SATS];
};

void nmea_begin();
void nmea_feed_bytes(const uint8_t* data, size_t len, uint32_t nowMs);
bool nmea_get_snapshot(NmeaGpsSnapshot& out);
bool nmea_get_last_gga(String& out);
#endif