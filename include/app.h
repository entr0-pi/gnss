#pragma once

#include <Arduino.h>

// Centralized build flags (override via build_flags in platformio.ini).
#ifndef WEBUI_ENABLE
#define WEBUI_ENABLE 1
#endif

#ifndef NMEA_ENABLE
#define NMEA_ENABLE 0
#endif

// ---------------- Dependency rule ----------------
// If Web UI is disabled, NMEA must be disabled too.
#if WEBUI_ENABLE == 0 && NMEA_ENABLE != 0
  #warning "NMEA_ENABLE forced to 0 because WEBUI_ENABLE=0"
  #undef  NMEA_ENABLE
  #define NMEA_ENABLE 0
#endif

#if WEBUI_ENABLE
// ---------------- STA config (Hotspot) ----------------
// These are the credentials and the static network config for STA mode.
// WiFi.config() sets a fixed IP, gateway, subnet, and DNS for the station interface.
static const char* STA_SSID = "64NDPVIWJCMG7RUZ9392";
static const char* STA_PASS = "azerty1234";
static const IPAddress STA_IP     (172, 20, 10, 2);
static const IPAddress STA_GW     (172, 20, 10, 1);
static const IPAddress STA_SUBNET (255, 255, 255, 240);
static const IPAddress STA_DNS    (172, 20, 10, 1);
#endif

// ---------------- BT ----------------
// BLE advertising name shown on the phone.
#ifndef BLE_DEVICE_NAME
#define BLE_DEVICE_NAME "UM980-BLE"
#endif
static const char DEVICE_NAME[] = BLE_DEVICE_NAME;
// Requested ATT MTU. Higher MTU can reduce overhead for a stream like NMEA.
#ifndef BLE_MTU_CFG
#define BLE_MTU_CFG 23
#endif
static const uint16_t BLE_MTU = BLE_MTU_CFG;

// UM980 output rate (Hz); used to derive low-rate BLE throttle.
#ifndef UM980_HZ_CFG
#define UM980_HZ_CFG 1
#endif
static const uint16_t UM980_HZ = UM980_HZ_CFG;

// BLE notify sizing and low-rate throttle derived from MTU and UM980 rate.
static const size_t BLE_MAX_PAYLOAD = BLE_MTU - 3;
static const size_t BLE_LOW_RATE_THRESHOLD = BLE_MAX_PAYLOAD / 2;
static const uint16_t BLE_LOW_RATE_DELAY_MS =
    ((1000 / (4 * UM980_HZ)) < 100) ? (1000 / (4 * UM980_HZ)) : 100;

// ---------------- UART ----------------
// Hardware UART pins connected to the UM980.
static const int PIN_UM980_RX = 20;        // ESP32 RX  (UM980 TX)
static const int PIN_UM980_TX = 21;        // ESP32 TX  (UM980 RX)
static const uint32_t UM980_BAUD = 115200; // UM980 serial baud rate

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
//   Buffer for UM980 -> phone stream (mostly NMEA, continuous).
static const size_t SB_UART_TO_BLE_SIZE = 4096;

// SB_BLE_TO_UART_SIZE:
//   Buffer for phone -> UM980 stream (RTCM bursts can be large and spiky).
//   Increase if RAM allows and you see drops.
static const size_t SB_BLE_TO_UART_SIZE = 16384;

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
