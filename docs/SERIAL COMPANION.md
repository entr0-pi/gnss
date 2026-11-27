# USB Serial Configuration: Firmware Implementation

How the firmware implements the USB configuration protocol. The protocol itself (frame format, operations,
error codes, settings) is specified in [SERIAL.md](SERIAL.md). The tags in brackets in the code (`[G4]`,
`[PMATCH]`...) are node ids from its [appendix flowchart](SERIAL.md#appendix-board-receiver-flowchart).

## Files

| File | Role |
|---|---|
| [include/serial_config.h](../include/serial_config.h) | Public API: `SERIAL_CONFIG_RX_BUFFER` (1024), `serial_config_setup()`, `serial_config_poll()` |
| [src/serial_config.cpp](../src/serial_config.cpp) | Whole implementation, in an anonymous namespace, compiled only when `SERIAL_CONFIG_ENABLE=1` |
| [include/app.h](../include/app.h) | Defines `SERIAL_CONFIG_ENABLE` (default `0`) and warns if `GLOBAL_LOG_LEVEL > 0` |
| [src/main.cpp](../src/main.cpp) | Calls the API at setup and in `loop()` |
| [platformio.ini](../platformio.ini) | Enables it with `-DSERIAL_CONFIG_ENABLE=1` |

## Integration in main.cpp

1. `initSerialAndConfig()` calls `Serial.setRxBufferSize(SERIAL_CONFIG_RX_BUFFER)` **before** `Serial.begin()`.
   The buffer must hold two full frames; a `static_assert` checks this.
2. `config_bootstrap()` runs first, then `serial_config_setup()`. Order matters: the keys bootstrap writes
   are the only keys `set` accepts.
3. `loop()` calls `serial_config_poll()` on every pass, before Wi-Fi, Web UI and NTRIP work. There is no
   dedicated task.

Serial is shared with the logger, so build with `GLOBAL_LOG_LEVEL=0`; otherwise log lines can corrupt
replies. `serial_config.cpp` never logs.

## Processing pipeline

`serial_config_poll()` → `pumpSerial()` → `handleFrame()` → `handleCommand()` → `executeCommand()` → `doSet` / `doGet` / `doErase`

| Function | Does | Spec |
|---|---|---|
| `pumpSerial()` | Drains `Serial`, assembles bytes between `STX` and `ETX`, drops oversize and stale frames | [§3](SERIAL.md#3-frame-format) |
| `handleFrame()` | Splits on the last `#`, checks the CRC, parses the JSON, requires `seq` | [§3](SERIAL.md#3-frame-format), [§10](SERIAL.md#10-error-codes) |
| `handleCommand()` | `hello`, session state, reply cache, replay check | [§5](SERIAL.md#5-session-and-sequence-numbers) |
| `executeCommand()` | Checks op, key, type, namespace, hidden keys, locked namespaces; opens the namespace | [§8](SERIAL.md#8-rules-enforced-by-the-board) |
| `doSet()` / `doGet()` / `doErase()` | NVS access through `Preferences`, opened per command (read-only for `get`) | [§4](SERIAL.md#4-requests-and-replies), [§6](SERIAL.md#6-value-types) |
| `buildFrame()` | Frames the reply; replaces a reply too large for one frame with a short `unsupported_value` error | [§3](SERIAL.md#3-frame-format) |
| `sendAndCacheReply()` / `sendReply()` | Writes the reply, cached for accepted commands, not cached for `hello` and sequence or frame errors | [§10](SERIAL.md#10-error-codes) |

## Design choices

- **Store only, apply nothing.** `onKeySet()` is an empty hook. `ntrip` changes are picked up by
  `configMonitorTask` (~5 s, when online); `gnss` and `wifi` take effect at the next boot. If the hook is
  ever filled, keep it short: it runs before the reply is sent.
- **No key table.** The keys written by `config_bootstrap()` define what can be set, so the key list lives in
  one place. Only the namespace list and the hidden NTRIP lockout keys are hard-coded.
- **Locked namespaces come from `app.h`.** `isImmutable()` uses `IMMUTABLE_UART`, `IMMUTABLE_WIFI` and
  `IMMUTABLE_NTRIP` (see [FLAGS.md](FLAGS.md#immutability-derived-read-only)).
- **Fixed memory.** Three static 512-byte buffers (RX, TX scratch, reply cache), no `String`, short-lived
  `JsonDocument`s.
- **Compiled out by default.** With `SERIAL_CONFIG_ENABLE=0`, the header is not included and the .cpp is empty.
