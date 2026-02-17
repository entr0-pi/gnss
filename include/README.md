
This directory holds project header files. Configuration parameters live in
`include/app.h`. WiFi secrets are defined in `include/secrets.h` (ignored by
git); copy `include/secrets.example.h` to get started.

## Parameters in app.h

Build flags (override in `platformio.ini`):
- `WEBUI_ENABLE` (default `0`): enable WiFi/Web UI.
- `NMEA_ENABLE` (default `0`): enable NMEA parsing.
- `TCP_ENABLE` (default `0`): enable TCP mirror.
- `NTRIP_CLIENT_ENABLE` (default `0`): enable NTRIP client support (auto-enables WiFi; compile error if `WIFI_ENABLE=0`).
- `BLE_ENABLE` (default `0`): enable BLE bridge.
- `WIFI_ENABLE` (default `WEBUI_ENABLE || TCP_ENABLE || NTRIP_CLIENT_ENABLE`): enable WiFi STA (required for web UI, TCP, or NTRIP).
- `WIFI_DUAL_MODE` (default `0`): enable STA + softAP.
- `STA_CHANNEL` (default `0`): optional fixed STA channel (`0` = auto).
- `SOFTAP_SSID_VALUE` (default `"GNSS-ESP32-AP"`): softAP SSID when dual mode is enabled.
- `SOFTAP_PASS_VALUE` (default `""`): softAP password (empty = open AP).
- `SOFTAP_CHANNEL` (default `6`): softAP channel.
- `SOFTAP_HIDDEN` (default `0`): hide softAP SSID when set to `1`.
- `SOFTAP_MAX_CONN` (default `2`): max softAP clients.
- `FORCE_WIFI_SECRETS` (default `0`): force WiFi credentials from `include/secrets.h` and make WiFi config immutable at runtime.
- `FORCE_HARDCODED_UART` (default `0`): lock UART config to compile-time pins/baud.
- `HARD_RX_PIN` (required when `FORCE_HARDCODED_UART=1`).
- `HARD_TX_PIN` (required when `FORCE_HARDCODED_UART=1`).
- `HARD_BAUD` (required when `FORCE_HARDCODED_UART=1`).
- `BLE_DEVICE_NAME` (default `"GNSS-BLE"`): BLE advertising name.
- `BLE_MTU_CFG` (default `23`): requested BLE MTU.
- `GNSS_HZ_CFG` (default `1`): GNSS output rate in Hz.
- `NTRIP_CLIENT_ENABLE_TASK` (default `1`): run NTRIP in a dedicated task (`0` = taskless mode).
- `NTRIP_CLIENT_ENABLE_REV1_FALLBACK` (default `1`): retry with NTRIP Rev1 when Rev2 handshake fails.
- `NTRIP_CLIENT_PASSIVE_SCAN_BYTES` (default `128`): bytes scanned for passive RTCM health checks.

TCP:
- `TCP_PORT` (default `5000`): TCP server port.

WiFi STA (when `WEBUI_ENABLE=1`, usually set in `include/secrets.h`):
- `STA_SSID_VALUE`
- `STA_PASS_VALUE`
- `STA_IP_VALUE`
- `STA_GW_VALUE`
- `STA_SUBNET_VALUE`
- `STA_DNS_VALUE`

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

Build tester:
- Use `pio run -e full` to validate a full feature flag set (`WEBUI_ENABLE`, `NMEA_ENABLE`, `TCP_ENABLE`, `NTRIP_CLIENT_ENABLE`) in one build.
