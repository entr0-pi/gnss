# GNSS PlatformIO Project

## Overview
- Provide a clean PlatformIO-based firmware workspace for the GNSS device.
- Separate hardware-facing firmware from web assets and supporting scripts.
- Keep build, upload, and extension workflows simple.

## Features
- **PlatformIO configuration** with a single entry point in `platformio.ini`.
- **Firmware source layout** under `src/` and `include/` following PlatformIO conventions.
- **Shared libraries** kept in `lib/` for reusable components.
- **Web assets** stored in `web/` for any UI or hosted files.
- **Utility scripts** in `scripts/` for automation or tooling.

## Build and Configuration
### Build Flags
- `WEBUI_ENABLE` (default `1`): enables WiFi/WebServer status UI. When `0`, web UI code is excluded and `scripts/gzip_web.py` does not run.
- `NMEA_ENABLE` (default `0` unless set in an env): enables the optional NMEA parser. When `0`, bytes still stream over BLE but no parsing occurs.
- `BLE_DEVICE_NAME` (default `"GNSS-BLE"`): BLE advertising name override.
- `BLE_MTU_CFG` (default `23`): requested BLE MTU; if negotiated MTU is valid (>=23) it is used at runtime, otherwise this value is the fallback; used to derive max notify payload.
- `GNSS_HZ_CFG` (default `1`): GNSS output rate (Hz) used for low-rate throttling.

### BLE MTU and Throttling
- `BLE_MTU_CFG` in `include/app.h` is used to derive the max notify payload (`BLE_MAX_PAYLOAD = BLE_MTU - 3`).
- `BLE_NOTIFY_CHUNK` is tied to `BLE_MAX_PAYLOAD` to avoid oversize notifications.
- Low-rate throttling is derived from MTU and GNSS rate:
  - `BLE_LOW_RATE_THRESHOLD = BLE_MAX_PAYLOAD / 2`
  - `BLE_LOW_RATE_DELAY_MS = min(1000 / (4 * GNSS_HZ), 100)`

### WiFi + BLE Coexistence Warning
- When `WEBUI_ENABLE=1` and BLE is enabled, **WiFi modem sleep must be enabled**.
- If `WiFi.setSleep(false)` is used, the ESP32-C3 can abort with:
  `Error! Should enable WiFi modem sleep when both WiFi and Bluetooth are enabled!!!!!!`

### Web UI Assets
- When `WEBUI_ENABLE=1`, the build runs `scripts/gzip_web.py` to regenerate `include/app_*.h` from `web/*`.
- After editing `web/index.html`, `web/app.js`, or `web/style.css`, rebuild so the headers are refreshed.

## UART, BLE, and Buffer Flow
The ESP32-C3 firmware acts as a transparent byte-stream bridge between the GNSS UART and a BLE client (phone/tablet). It uses the Nordic UART Service (NUS) for BLE and FreeRTOS StreamBuffers as ring buffers to decouple producer/consumer timing.

### UART (ESP32 -> GNSS)
- **Pins:** ESP32 GPIO20 = RX (connected to GNSS TX), GPIO21 = TX (connected to GNSS RX).
- **Baud:** 115200 (matches GNSS serial baud rate).
- **Payload:** Raw GNSS output (NMEA and any other serial bytes). There is no framing or parsing required for the pass-through path.

### BLE (ESP32 -> Client)
- **Service:** Nordic UART Service (NUS).
- **Characteristics:**
  - **RX (phone -> ESP32):** WRITE/WRITE_NR, used for RTCM or other inbound bytes.
  - **TX (ESP32 -> phone):** NOTIFY, used to stream GNSS output to the client.
- **MTU:** Requested MTU is 23 in `include/app.h` (adjust to match your phone/app behavior).

### Stream Buffers (Decoupling and Backpressure)
The firmware uses two FreeRTOS StreamBuffers (ring buffers) to handle bursty traffic and to avoid blocking BLE/UART tasks:

1. **UART -> BLE buffer**
   - **Purpose:** Stores bytes read from the GNSS UART until the BLE notify task can send them.
   - **Size:** 4096 bytes (tuned for continuous NMEA output).
   - **Flow:** UART RX task pushes bytes -> BLE TX task pulls bytes -> BLE notifications.

2. **BLE -> UART buffer**
   - **Purpose:** Stores bytes written by the client (typically RTCM corrections) until the UART TX task can forward them.
   - **Size:** 16384 bytes (larger to handle spiky RTCM bursts).
   - **Flow:** BLE write callback pushes bytes -> UART TX task pulls bytes -> GNSS UART.

### Backpressure and Drops
- BLE notifications are **paced** to avoid overloading the BLE stack.
- If a buffer fills, new bytes are **dropped** (best-effort). Drop counters are exposed in the UI.
- The BLE TX task will **not drain** the UART buffer when the client is disconnected or hasn't enabled notifications, so the client sees the most current data when it reconnects.

### Drops Tuning (when UI shows drops)
- **UART->BLE drops**: increase `SB_UART_TO_BLE_SIZE` or reduce BLE send rate (increase `BLE_TX_WAIT_TICKS` / `BLE_LOW_RATE_DELAY_MS`).
- **BLE->UART drops**: increase `SB_BLE_TO_UART_SIZE` or lower burst size on the phone/app side.
- **Large MTU**: if your phone supports it, raise `BLE_MTU_CFG` to increase per-notify payload (rebuild required).
- After tuning, rebuild so the new constants take effect.

### End-to-End Summary
```
GNSS UART TX -> ESP32 UART RX -> [UART->BLE StreamBuffer] -> BLE NOTIFY -> Client
Client BLE WRITE -> [BLE->UART StreamBuffer] -> ESP32 UART TX -> GNSS UART RX
```

## Folder Structure
```
.
├── include/        # Header files and shared declarations
├── lib/            # Reusable libraries
├── scripts/        # Build or development helper scripts
├── src/            # Firmware source files (main entry points)
├── web/            # Web UI or static assets
├── platformio.ini  # PlatformIO project configuration
└── README.md       # Project documentation
```

## Additional Information
- Build and upload with PlatformIO using the standard `pio run` and `pio run -t upload` commands.
- When adding new code, prefer keeping device logic in `src/` and generic helpers in `lib/`.
- If you add a frontend, keep assets in `web/` and document any build steps here.
