
This directory holds project header files. Configuration parameters live in
`include/app.h`. WiFi secrets are defined in `include/secrets.h` (ignored by
git); copy `include/secrets.example.h` to get started.

## Parameters in app.h

Build flags (override in `platformio.ini`):
- `WEBUI_ENABLE` (default `0`): enable WiFi/Web UI.
- `NMEA_ENABLE` (default `0`): enable NMEA parsing.
- `TCP_ENABLE` (default `0`): enable TCP mirror.
- `WIFI_ENABLE` (default `WEBUI_ENABLE || TCP_ENABLE`): enable WiFi STA (required for web UI or TCP).
- `FORCE_WIFI_SECRETS` (default `0`): force WiFi credentials from `include/secrets.h` and skip `/wifi.json`.
- `FORCE_HARDCODED_UART` (default `0`): lock UART config to compile-time pins/baud.
- `HARD_RX_PIN` (required when `FORCE_HARDCODED_UART=1`).
- `HARD_TX_PIN` (required when `FORCE_HARDCODED_UART=1`).
- `HARD_BAUD` (required when `FORCE_HARDCODED_UART=1`).
- `BLE_DEVICE_NAME` (default `"GNSS-BLE"`): BLE advertising name.
- `BLE_MTU_CFG` (default `23`): requested BLE MTU.
- `GNSS_HZ_CFG` (default `1`): GNSS output rate in Hz.

TCP:
- `TCP_PORT` (default `5000`): TCP server port.

WiFi STA (when `WEBUI_ENABLE=1`, usually set in `include/secrets.h`):
- `STA_SSID`
- `STA_PASS`
- `STA_IP`
- `STA_GW`
- `STA_SUBNET`
- `STA_DNS`

BLE/MTU derived:
- `BLE_MTU`
- `BLE_MAX_PAYLOAD`
- `BLE_LOW_RATE_THRESHOLD`
- `BLE_LOW_RATE_DELAY_MS`

UART:
- `PIN_GNSS_RX`
- `PIN_GNSS_TX`
- `GNSS_BAUD`

Serial:
- `SERIAL_BAUD`

Tunables:
- `BLE_NOTIFY_CHUNK`
- `UART_CHUNK`
- `SB_UART_TO_BLE_SIZE`
- `SB_BLE_TO_UART_SIZE`
- `SB_UART_TO_TCP_SIZE` (when `TCP_ENABLE=1`)
- `SB_TCP_TO_UART_SIZE` (when `TCP_ENABLE=1`)
- `SB_TRIGGER_LEVEL`
- `BLE_TX_WAIT_TICKS`
- `BLE_OK_DELAY`
- `BLE_FAIL_DELAY`
