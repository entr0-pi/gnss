// ======================= web_ui.h (FINAL - snapshot method) =======================
#pragma once

#include <Arduino.h>
#include "app.h"

#if WEBUI_ENABLE
#include <WebServer.h>
#else
class WebServer;
#endif
#include <IPAddress.h>

void webui_begin(WebServer& server, const IPAddress& sta_dns);

// Last known internet reachability from /api/status probes.
#if WEBUI_ENABLE
bool webui_get_internet_reachable();
#else
inline bool webui_get_internet_reachable() { return false; }
#endif

// ---- BLE snapshot interface (implemented in main.cpp) ----
struct WebuiBleSnapshot {
  bool     connected;
  uint16_t mtu;
  uint32_t txBytes;          // truncated from uint64_t
  uint32_t rxBytes;          // truncated from uint64_t
  uint32_t uart2bleDrops;    // drops pushing UART->BLE stream buffer
  uint32_t ble2uartDrops;    // drops pushing BLE->UART stream buffer
};

// ---- TCP snapshot interface (implemented in main.cpp) ----
#if TCP_ENABLE
struct WebuiTcpSnapshot {
  bool     connected;
  uint32_t txBytes;          // truncated from uint64_t
  uint32_t rxBytes;          // truncated from uint64_t
  uint32_t uart2tcpDrops;    // drops pushing UART->TCP stream buffer
  uint32_t tcp2uartDrops;    // drops pushing TCP->UART stream buffer
};
#endif

// ---- GPS snapshot interface (implemented in main.cpp) ----
#if NMEA_ENABLE

#ifndef NMEA_MAX_SATS
#define NMEA_MAX_SATS 48
#endif

// Per-satellite info for the web UI (mirrors NmeaSatInfo from parsing_nmea.h).
struct WebuiSatInfo {
  int16_t  nr;            // satellite PRN number
  int16_t  elevation;     // degrees above horizon (0-90)
  int16_t  azimuth;       // degrees from true north (0-359)
  int16_t  snr;           // signal-to-noise ratio (dBHz), 0 = not tracked
  uint8_t  constellation; // 0=GPS, 1=GLONASS, 2=Galileo, 3=BeiDou, 4=other
};

struct WebuiGpsSnapshot {
  bool     valid;
  double   lat;
  double   lon;
  float    speedKmh;

  uint8_t  satsUsed;
  uint8_t  fixQuality;
  uint8_t  fixType;
  float    hdop;

  float    hAcc_m;
  float    vAcc_m;
  uint8_t  accSource; // 0=none, 1=GST, 2=HDOP-est

  bool     timeValid;
  uint8_t  hour, minute, second;

  uint16_t year;
  uint8_t  month, day;

  uint32_t ageMs;

  // Satellite details from GSV
  uint8_t      satCount;
  WebuiSatInfo sats[NMEA_MAX_SATS];
};
#endif

// Returns true and fills `out` with a snapshot of BLE and GPS status.
bool webui_get_ble_snapshot(WebuiBleSnapshot& out);
#if TCP_ENABLE
bool webui_get_tcp_snapshot(WebuiTcpSnapshot& out);
#endif
#if NMEA_ENABLE
bool webui_get_gps_snapshot(WebuiGpsSnapshot& out);
#endif

#if NTRIP_CLIENT_ENABLE
struct WebuiNtripSnapshot {
  bool connected;
  bool healthy;
  bool streaming;
  uint32_t bytesReceived;
  uint32_t totalFrames;
  uint16_t lastMessageType;
  uint32_t lastFrameAgeMs;
  uint8_t protocolVersion;  // 1 = Rev1, 2 = Rev2, 0 = not connected
};
bool webui_get_ntrip_snapshot(WebuiNtripSnapshot& out);
#endif
