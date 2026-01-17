# UM980 PlatformIO Project

## Goals
- Provide a clean PlatformIO-based firmware workspace for the UM980 device.
- Separate hardware-facing firmware from web assets and supporting scripts.
- Make it straightforward to build, upload, and extend the project structure.

## Features
- **PlatformIO configuration** with a single entry point in `platformio.ini`.
- **Firmware source layout** under `src/` and `include/` following PlatformIO conventions.
- **Shared libraries** kept in `lib/` for reusable components.
- **Web assets** stored in `web/` for any UI or hosted files.
- **Utility scripts** in `scripts/` for automation or tooling.


## Build Flags
- `WEBUI_ENABLE` (default `1`): enables WiFi/WebServer status UI. When `0`, web UI code is excluded and `scripts/gzip_web.py` does not run.
- `NMEA_ENABLE` (default `0` unless set in an env): enables the optional NMEA parser. When `0`, bytes still stream over BLE but no parsing occurs.

## UART, BLE, and Buffer Flow (ESP32 ↔ UM980 ↔ Client)
The ESP32-C3 firmware acts as a transparent byte-stream bridge between the UM980 UART and a BLE client (phone/tablet). It uses the Nordic UART Service (NUS) for BLE and FreeRTOS StreamBuffers as ring buffers to decouple producer/consumer timing.

### UART (ESP32 ↔ UM980)
- **Pins:** ESP32 GPIO20 = RX (connected to UM980 TX), GPIO21 = TX (connected to UM980 RX).
- **Baud:** 115200 (matches UM980 serial baud rate).
- **Payload:** Raw UM980 output (NMEA and any other serial bytes). There is no framing or parsing required for the pass-through path.

### BLE (ESP32 ↔ Client)
- **Service:** Nordic UART Service (NUS).
- **Characteristics:**
  - **RX (phone → ESP32):** WRITE/WRITE_NR, used for RTCM or other inbound bytes.
  - **TX (ESP32 → phone):** NOTIFY, used to stream UM980 output to the client.
- **MTU:** Requested MTU is 185 to reduce overhead for streaming data.

### Stream Buffers (Decoupling and Backpressure)
The firmware uses two FreeRTOS StreamBuffers (ring buffers) to handle bursty traffic and to avoid blocking BLE/UART tasks:

1. **UART → BLE buffer**
   - **Purpose:** Stores bytes read from the UM980 UART until the BLE notify task can send them.
   - **Size:** 4096 bytes (tuned for continuous NMEA output).
   - **Flow:** UART RX task pushes bytes → BLE TX task pulls bytes → BLE notifications.

2. **BLE → UART buffer**
   - **Purpose:** Stores bytes written by the client (typically RTCM corrections) until the UART TX task can forward them.
   - **Size:** 16384 bytes (larger to handle spiky RTCM bursts).
   - **Flow:** BLE write callback pushes bytes → UART TX task pulls bytes → UM980 UART.

### Backpressure and Drops
- BLE notifications are **paced** to avoid overloading the BLE stack.
- If a buffer fills, new bytes are **dropped** (best-effort, no counters).
- The BLE TX task will **not drain** the UART buffer when the client is disconnected or hasn’t enabled notifications, so the client sees the most current data when it reconnects.

### End-to-End Summary
```
UM980 UART TX ──> ESP32 UART RX ──> [UART→BLE StreamBuffer] ──> BLE NOTIFY ──> Client
Client BLE WRITE ──> [BLE→UART StreamBuffer] ──> ESP32 UART TX ──> UM980 UART RX
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
