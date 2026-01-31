
This directory holds project header files. Configuration parameters live in
`include/app.h`. Update that file (or override with PlatformIO build flags)
to change device behavior.

## Parameters in app.h

Build flags (override in `platformio.ini`):
- `WEBUI_ENABLE` (default `1`): enable WiFi/Web UI.
- `NMEA_ENABLE` (default `0`): enable NMEA parsing.
- `TCP_ENABLE` (default `1`): enable TCP mirror.
- `BLE_DEVICE_NAME` (default `"GNSS-BLE"`): BLE advertising name.
- `BLE_MTU_CFG` (default `23`): requested BLE MTU.
- `GNSS_HZ_CFG` (default `1`): GNSS output rate in Hz.

TCP:
- `TCP_PORT` (default `5000`): TCP server port.

WiFi STA (when `WEBUI_ENABLE=1`):
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
