# Build Flags Reference

All flags are set via `-D` in `platformio.ini` `build_flags`.
Defaults are defined in [`include/app.h`](../include/app.h) unless noted otherwise.

---

## Feature Toggles

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `WEBUI_ENABLE` | bool | `0` | Enable the web UI (HTTP server, `/api/status`, config endpoints). |
| `NMEA_ENABLE` | bool | `0` | Enable NMEA sentence parsing (skyplot, accuracy, fix info). |
| `TCP_ENABLE` | bool | `0` | Enable a raw TCP server for GNSS data streaming. |
| `NTRIP_CLIENT_ENABLE` | bool | `0` | Enable the NTRIP corrections client. |
| `BLE_ENABLE` | bool | `0` | Enable Bluetooth Low Energy GNSS streaming. |

### Dependencies

```
NMEA_ENABLE ──requires──▶ WEBUI_ENABLE
    (forced to 0 with warning if WEBUI_ENABLE=0)

NTRIP_CLIENT_ENABLE ──requires──▶ WEBUI_ENABLE
    (hard error if WEBUI_ENABLE=0)
```

### Derived flag

| Flag | Derivation | Description |
|------|------------|-------------|
| `WIFI_ENABLE` | `WEBUI_ENABLE \|\| TCP_ENABLE \|\| NTRIP_CLIENT_ENABLE` | Auto-enabled when any WiFi-dependent feature is on. Can be overridden to `0` to force-disable. |

---

## NTRIP Library

These are consumed by `lib/ntrip-client` (also declared in its own header with the same defaults).

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `NTRIP_CLIENT_ENABLE_TASK` | bool | `1` | `1` = run NTRIP in a background FreeRTOS task. `0` = manual `loop()` mode. Requires ESP32. |
| `NTRIP_CLIENT_ENABLE_REV1_FALLBACK` | bool | `1` | `1` = if Rev2 handshake fails, retry with Rev1. `0` = Rev2 only. |
| `NTRIP_CLIENT_PASSIVE_SCAN_BYTES` | int | `128` | Number of bytes scanned during passive health checks for the RTCM preamble (`0xD3`). |

---

## WiFi

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `WIFI_DUAL_MODE` | bool | `0` | `0` = STA only. `1` = STA + SoftAP (dual mode). |
| `STA_CHANNEL` | int | `0` | Fixed STA channel (1-13) for quicker association. `0` = auto. |
| `FORCE_WIFI_SECRETS` | bool | `0` | `1` = use hardcoded credentials from `include/secrets.h`. `0` = load from NVS (configurable via web UI). |

### SoftAP (only used when `WIFI_DUAL_MODE=1`)

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `SOFTAP_SSID_VALUE` | string | `"GNSS-ESP32-AP"` | Access point SSID. |
| `SOFTAP_PASS_VALUE` | string | `""` (open) | Access point password. Empty = open network. |
| `SOFTAP_CHANNEL` | int | `6` | AP channel (1-13). |
| `SOFTAP_HIDDEN` | bool | `0` | `1` = hide SSID from broadcast. |
| `SOFTAP_MAX_CONN` | int | `2` | Maximum simultaneous AP clients. |

### Hardcoded WiFi credentials (`FORCE_WIFI_SECRETS=1`)

Requires a file `include/secrets.h` defining:

| Define | Type | Example |
|--------|------|---------|
| `STA_SSID_VALUE` | string | `"MyNetwork"` |
| `STA_PASS_VALUE` | string | `"MyPassword"` |
| `STA_IP_VALUE` | `IPAddress` | `IPAddress(192,168,1,50)` |
| `STA_GW_VALUE` | `IPAddress` | `IPAddress(192,168,1,1)` |
| `STA_SUBNET_VALUE` | `IPAddress` | `IPAddress(255,255,255,0)` |
| `STA_DNS_VALUE` | `IPAddress` | `IPAddress(8,8,8,8)` |

All six are required or the build will fail.

---

## UART / GNSS Hardware

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `FORCE_HARDCODED_UART` | bool | `0` | `1` = use compile-time pin/baud values. `0` = configurable via web UI / NVS. |
| `HARD_RX_PIN` | int | *(required)* | GNSS UART RX pin. Only used when `FORCE_HARDCODED_UART=1`. |
| `HARD_TX_PIN` | int | *(required)* | GNSS UART TX pin. Only used when `FORCE_HARDCODED_UART=1`. |
| `HARD_BAUD` | int | *(required)* | GNSS UART baud rate. Only used when `FORCE_HARDCODED_UART=1`. |

All three `HARD_*` flags are mandatory when `FORCE_HARDCODED_UART=1` (build error otherwise).

---

## Immutability (derived, read-only)

These are derived in `app.h` and **cannot be set directly**. They control whether the web UI allows runtime config changes for each subsystem.

| Flag | Derivation | Effect when `1` |
|------|------------|-----------------|
| `IMMUTABLE_UART` | `FORCE_HARDCODED_UART` | GNSS UART config is locked. Web UI POST rejected, NVS defaults not seeded. |
| `IMMUTABLE_WIFI` | `FORCE_WIFI_SECRETS \|\| !WIFI_ENABLE` | WiFi config is locked. |
| `IMMUTABLE_NTRIP` | `!NTRIP_CLIENT_ENABLE` | NTRIP config is locked. |

---

## BLE

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `BLE_DEVICE_NAME` | string | `"GNSS-BLE"` | BLE advertising name visible on the phone. |
| `BLE_MTU_CFG` | int | `23` | Requested ATT MTU. Higher values reduce per-packet overhead. |
| `GNSS_HZ_CFG` | int | `1` | GNSS output rate in Hz. Used to derive `BLE_LOW_RATE_DELAY_MS` for notification pacing. |

---

## Logging

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `GLOBAL_LOG_LEVEL` | int | `0` | `0`=SILENT, `1`=ERROR, `2`=WARNING, `3`=INFO, `4`=DEBUG. Controls which `LOG_E`/`LOG_W`/`LOG_I`/`LOG_D` macros emit output. |
| `LOG_USE_COLOR` | bool | `0` | `1` = enable ANSI color codes in serial log output. |
| `MODULE_LOG` | bool | per-file | Defined per `.cpp` file (`#define MODULE_LOG 1` before `#include "logger.h"`). `0` = suppress that module's logs even if `GLOBAL_LOG_LEVEL` would allow them. |

---

## Network

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `TCP_PORT` | int | `5000` | TCP server listen port (when `TCP_ENABLE=1`). |
| `NMEA_MAX_SATS` | int | `48` | Maximum satellites tracked in GSV arrays. Defined in [`include/web_ui.h`](../include/web_ui.h) and [`include/nmea_gps.h`](../include/nmea_gps.h). |

---

## App Metadata

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `APP_NAME` | string | `"GNSS-ESP32"` | Application display name. |
| `APP_VERSION` | string | `"1.0.0"` | Firmware version string. |

---

## NVS Schema

Defined in [`include/nvs_keys.h`](../include/nvs_keys.h). Each config module asserts against these at build time.

| Flag | Value | Description |
|------|-------|-------------|
| `NVS_SCHEMA_VERSION` | `1` | Increment when adding/removing/renaming NVS keys. |
| `NVS_GNSS_REQUIRED_KEYS` | `3` | `rx_pin`, `tx_pin`, `baud` |
| `NVS_WIFI_REQUIRED_KEYS` | `8` | `ssid`, `pass`, `dhcp`, `ip`, `gw`, `subnet`, `dns`, `accesspoint` |
| `NVS_NTRIP_REQUIRED_KEYS` | `16` | 13 config keys + 3 lockout keys |

---

## Quick Reference: Common Profiles

**Full feature set (development):**
```ini
build_flags =
  -DGLOBAL_LOG_LEVEL=4
  -DWEBUI_ENABLE=1
  -DNMEA_ENABLE=1
  -DTCP_ENABLE=1
  -DNTRIP_CLIENT_ENABLE=1
  -DBLE_ENABLE=1
  -DWIFI_DUAL_MODE=1
```

**Minimal NTRIP rover (production):**
```ini
build_flags =
  -DGLOBAL_LOG_LEVEL=0
  -DWEBUI_ENABLE=1
  -DNTRIP_CLIENT_ENABLE=1
  -DFORCE_HARDCODED_UART=1
  -DHARD_RX_PIN=20
  -DHARD_TX_PIN=21
  -DHARD_BAUD=115200
```

**BLE-only (no WiFi):**
```ini
build_flags =
  -DBLE_ENABLE=1
  -DFORCE_HARDCODED_UART=1
  -DHARD_RX_PIN=20
  -DHARD_TX_PIN=21
  -DHARD_BAUD=115200
```
