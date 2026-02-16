#pragma once

#include <Arduino.h>

// Centralized build flags (override via build_flags in platformio.ini).
#ifndef WEBUI_ENABLE
#define WEBUI_ENABLE 0
#endif

#ifndef NMEA_ENABLE
#define NMEA_ENABLE 0
#endif

#ifndef TCP_ENABLE
#define TCP_ENABLE 0
#endif

#ifndef NTRIP_CLIENT_ENABLE
#define NTRIP_CLIENT_ENABLE 0
#endif

#ifndef BLE_ENABLE
#define BLE_ENABLE 0
#endif

#ifndef TCP_PORT
#define TCP_PORT 5000
#endif

// ---------------- NTRIP library flags ----------------
// These are consumed by lib/ntrip-client (NtripClient.h).
// Keep them centralized here so build_flags only need one include point.
//
// NTRIP_CLIENT_ENABLE_TASK:
//   1 = run NTRIP in a background FreeRTOS task (ESP32 default)
//   0 = taskless mode (manual loop)
#ifndef NTRIP_CLIENT_ENABLE_TASK
#define NTRIP_CLIENT_ENABLE_TASK 1
#endif

// NTRIP_CLIENT_ENABLE_REV1_FALLBACK:
//   1 = if NTRIP Rev2 handshake fails, retry with Rev1
//   0 = Rev2 only
#ifndef NTRIP_CLIENT_ENABLE_REV1_FALLBACK
#define NTRIP_CLIENT_ENABLE_REV1_FALLBACK 1
#endif

// NTRIP_CLIENT_PASSIVE_SCAN_BYTES:
//   Number of bytes scanned during passive health checks for RTCM preamble.
#ifndef NTRIP_CLIENT_PASSIVE_SCAN_BYTES
#define NTRIP_CLIENT_PASSIVE_SCAN_BYTES 128
#endif
// ---------------- END NTRIP library flags ----------------

#ifndef WIFI_ENABLE
#define WIFI_ENABLE (WEBUI_ENABLE || TCP_ENABLE || NTRIP_CLIENT_ENABLE)
#endif

// WiFi operating mode:
// 0 = STA only
// 1 = dual mode (STA + softAP)
#ifndef WIFI_DUAL_MODE
#define WIFI_DUAL_MODE 0
#endif

// Optional fixed STA channel for quicker association.
// 0 = automatic channel selection (default)
#ifndef STA_CHANNEL
#define STA_CHANNEL 0
#endif

// SoftAP defaults (used when WIFI_DUAL_MODE=1).
#ifndef SOFTAP_SSID_VALUE
#define SOFTAP_SSID_VALUE "GNSS-ESP32-AP"
#endif

#ifndef SOFTAP_PASS_VALUE
#define SOFTAP_PASS_VALUE ""
#endif

#ifndef SOFTAP_CHANNEL
#define SOFTAP_CHANNEL 6
#endif

#ifndef SOFTAP_HIDDEN
#define SOFTAP_HIDDEN 0
#endif

#ifndef SOFTAP_MAX_CONN
#define SOFTAP_MAX_CONN 2
#endif

#if WIFI_ENABLE && WIFI_DUAL_MODE
static const char* SOFTAP_SSID = SOFTAP_SSID_VALUE;
static const char* SOFTAP_PASS = SOFTAP_PASS_VALUE;
#endif

#ifndef FORCE_WIFI_SECRETS
#define FORCE_WIFI_SECRETS 0
#endif

#ifndef GLOBAL_LOG_LEVEL
// 0:SILENT, 1:ERROR, 2:WARNING, 3:INFO, 4:DEBUG
#define GLOBAL_LOG_LEVEL 0
#endif

#ifndef LOG_USE_COLOR
#define LOG_USE_COLOR 0
#endif

#ifndef APP_NAME
#define APP_NAME    "GNSS-ESP32"
#endif

#ifndef APP_VERSION
#define APP_VERSION "1.0.0"
#endif

// ---------------- Dependency rule ----------------
// If Web UI is disabled, NMEA must be disabled too.
#if WEBUI_ENABLE == 0 && NMEA_ENABLE != 0
  #warning "NMEA_ENABLE forced to 0 because WEBUI_ENABLE=0"
  #undef  NMEA_ENABLE
  #define NMEA_ENABLE 0
#endif

// NTRIP depends on WiFi.
#if NTRIP_CLIENT_ENABLE != 0 && WIFI_ENABLE == 0
  #error "NTRIP_CLIENT_ENABLE requires WIFI_ENABLE=1"
#endif

#if WIFI_ENABLE && FORCE_WIFI_SECRETS
  // ---------------- STA config (Hotspot) ----------------
  // These are the credentials and the static network config for STA mode.
  // WiFi.config() sets a fixed IP, gateway, subnet, and DNS for the station interface.
  #if __has_include("secrets.h")
  #include "secrets.h"
  #endif

  #if !defined(STA_SSID_VALUE) || !defined(STA_PASS_VALUE) || !defined(STA_IP_VALUE) || \
      !defined(STA_GW_VALUE) || !defined(STA_SUBNET_VALUE) || !defined(STA_DNS_VALUE)
    #error "Define STA_*_VALUE in include/secrets.h"
  #endif

  static const char* STA_SSID = STA_SSID_VALUE;
  static const char* STA_PASS = STA_PASS_VALUE;
  static const IPAddress STA_IP     = STA_IP_VALUE;
  static const IPAddress STA_GW     = STA_GW_VALUE;
  static const IPAddress STA_SUBNET = STA_SUBNET_VALUE;
  static const IPAddress STA_DNS    = STA_DNS_VALUE;
#endif
#if WIFI_ENABLE && !FORCE_WIFI_SECRETS
  // Dummy defaults if not using secrets.h
  static const char* STA_SSID = "CHANGE_ME";
  static const char* STA_PASS = "CHANGE_ME";
  static const IPAddress STA_IP     = IPAddress(192, 168, 1, 200);
  static const IPAddress STA_GW     = IPAddress(192, 168, 1, 1);
  static const IPAddress STA_SUBNET = IPAddress(255, 255, 255, 0);
  static const IPAddress STA_DNS    = IPAddress(8, 8, 8, 8);
#endif

// ---------------- BT ----------------
// BLE advertising name shown on the phone.
#ifndef BLE_DEVICE_NAME
#define BLE_DEVICE_NAME "GNSS-BLE"
#endif

// Requested ATT MTU. Higher MTU can reduce overhead for a stream like NMEA.
#ifndef BLE_MTU_CFG
#define BLE_MTU_CFG 23
#endif
static const uint16_t BLE_MTU = BLE_MTU_CFG;

// GNSS output rate (Hz); used to derive low-rate BLE throttle.
#ifndef GNSS_HZ_CFG
#define GNSS_HZ_CFG 1
#endif
static const uint16_t GNSS_HZ = GNSS_HZ_CFG;

// BLE notify sizing and low-rate throttle derived from MTU and GNSS rate.
static const size_t BLE_MAX_PAYLOAD = BLE_MTU - 3;
static const size_t BLE_LOW_RATE_THRESHOLD = BLE_MAX_PAYLOAD / 2;
static const uint16_t BLE_LOW_RATE_DELAY_MS =
    ((1000 / (4 * GNSS_HZ)) < 100) ? (1000 / (4 * GNSS_HZ)) : 100;

// ---------------- UART ----------------
// Hardware UART pins connected to the GNSS receiver.
// Default to -1/0 (unconfigured) - user must configure via web UI
// Can be overridden at compile time via build_flags in platformio.ini

#ifndef FORCE_HARDCODED_UART
#define FORCE_HARDCODED_UART 0
#endif

#if FORCE_HARDCODED_UART
  // Forced hardcoded mode: use build flags
  #ifndef HARD_RX_PIN
    #error "FORCE_HARDCODED_UART requires HARD_RX_PIN to be defined"
  #endif
  #ifndef HARD_TX_PIN
    #error "FORCE_HARDCODED_UART requires HARD_TX_PIN to be defined"
  #endif
  #ifndef HARD_BAUD
    #error "FORCE_HARDCODED_UART requires HARD_BAUD to be defined"
  #endif

  static const int PIN_GNSS_RX = HARD_RX_PIN;
  static const int PIN_GNSS_TX = HARD_TX_PIN;
  static const uint32_t GNSS_BAUD = HARD_BAUD;
#endif

// ---------------- Immutability flags ----------------
// Per-subsystem immutability. When 1, the subsystem config cannot be changed
// at runtime (WebUI POST is rejected, NVS defaults are not seeded).
// These are the single source of truth -- do not re-derive elsewhere.
#define IMMUTABLE_UART   (FORCE_HARDCODED_UART)
#define IMMUTABLE_WIFI   (FORCE_WIFI_SECRETS || !WIFI_ENABLE)
#define IMMUTABLE_NTRIP  (!NTRIP_CLIENT_ENABLE)

// ---------------- SERIAL ----------------
// USB CDC serial used for debug logs in the Arduino monitor.
static const int SERIAL_BAUD = 115200;

// ---------------- Tunables ----------------
// BLE_NOTIFY_CHUNK:
//   Maximum bytes in a single notification payload we attempt to send.
//   Keep it <= (MTU - 3) to avoid oversize notifications.
static const size_t BLE_NOTIFY_CHUNK = BLE_MAX_PAYLOAD;

// UART_CHUNK:
//   Maximum bytes transferred per loop iteration for UART read/write buffers.
//   This is a local scratch buffer size; it does NOT change baud rate.
static const size_t UART_CHUNK = 256;

// Ring buffer sizes (StreamBuffers):
// SB_UART_TO_BLE_SIZE:
//   Buffer for GNSS -> phone stream (mostly NMEA, continuous).
static const size_t SB_UART_TO_BLE_SIZE = 4096;

// SB_BLE_TO_UART_SIZE:
//   Buffer for phone -> GNSS stream (RTCM bursts can be large and spiky).
//   Increase if RAM allows and you see drops.
static const size_t SB_BLE_TO_UART_SIZE = 16384;

#if TCP_ENABLE
// SB_UART_TO_TCP_SIZE:
//   Buffer for GNSS -> TCP stream (same content as BLE).
static const size_t SB_UART_TO_TCP_SIZE = 2048;

// SB_TCP_TO_UART_SIZE:
//   Buffer for TCP -> GNSS stream (RTCM bursts can be large and spiky).
static const size_t SB_TCP_TO_UART_SIZE = 4096;
#endif

#if NTRIP_CLIENT_ENABLE
// SB_NTRIP_TO_UART_SIZE:
//   Buffer for NTRIP corrections -> GNSS UART stream.
static const size_t SB_NTRIP_TO_UART_SIZE = 4096;
#endif

// StreamBuffer trigger level:
// A receiver task blocked on xStreamBufferReceive() will unblock once at least this
// many bytes are present (or timeout). 1 means "wake as soon as any byte arrives".
static const size_t SB_TRIGGER_LEVEL = 1;

// Backpressure pacing:
// We deliberately pace BLE notifications to avoid hammering the BLE stack.
// BLE_TX_WAIT_TICKS: wait time when pulling from UART->BLE buffer.
// BLE_OK_DELAY:      small delay after successful notify to yield.
// BLE_FAIL_DELAY:    longer delay after failure (phone not ready / congestion).
static const TickType_t BLE_TX_WAIT_TICKS = pdMS_TO_TICKS(50);
static const TickType_t BLE_OK_DELAY      = pdMS_TO_TICKS(1);
static const TickType_t BLE_FAIL_DELAY    = pdMS_TO_TICKS(15);

// ---------------- NMEA ----------------
// GSV buffer expiration: if a constellation hasn't updated within this window,
// its satellites are dropped from the "in view" list.
static const uint32_t NMEA_GSV_STALE_MS = 10000;

// Validate GSV message sequence (sequential msg_nr, consistent total_msgs).
// Rejects and resets the buffer on sequence errors.
// Disable (set to 0) if your receiver sends non-standard GSV sequences.
#ifndef NMEA_VALIDATE_GSV_SEQUENCE
#define NMEA_VALIDATE_GSV_SEQUENCE 1
#endif
