# Serial config mode: implementation companion

How the USB-serial NVS config protocol (used by the desktop tool) is wired into the firmware. The protocol spec itself is in [docs/SERIAL.md](docs/SERIAL.md); the tags in brackets in the code (`[G4]`, `[PMATCH]`, ...) are node ids from its flowchart.

## Files

| File | Role |
|---|---|
| [include/serial_config.h](include/serial_config.h) | Public API: `SERIAL_CONFIG_RX_BUFFER` (1024), `serial_config_setup()`, `serial_config_poll()` |
| [src/serial_config.cpp](src/serial_config.cpp) | Whole implementation, in an anonymous namespace, compiled only when `SERIAL_CONFIG_ENABLE=1` |
| [include/app.h](include/app.h) | Defines `SERIAL_CONFIG_ENABLE` (default `0`) and warns if `GLOBAL_LOG_LEVEL > 0` |
| [src/main.cpp](src/main.cpp) | Calls the API at setup and in `loop()` |
| [platformio.ini](platformio.ini) | Enables it with `-DSERIAL_CONFIG_ENABLE=1` |

## Integration in main.cpp

1. `initSerialAndConfig()` calls `Serial.setRxBufferSize(SERIAL_CONFIG_RX_BUFFER)` **before** `Serial.begin()` (the buffer must hold two full 512-byte frames; a `static_assert` checks this).
2. `config_bootstrap()` runs first, then `serial_config_setup()`. Order matters: the seeded NVS defaults *are* the schema `set` validates against.
3. `loop()` calls `serial_config_poll()` on every pass, before WiFi, web UI and NTRIP work. There is no dedicated task.

Serial is shared with the logger, so build with `GLOBAL_LOG_LEVEL=0`; otherwise log lines can corrupt replies. `serial_config.cpp` never logs.

## Processing pipeline

`serial_config_poll()` → `pumpSerial()` → `handleFrame()` → `handleCommand()` → `executeCommand()` → `doSet` / `doGet` / `doErase`.

1. **Byte receiver** (`pumpSerial`): drains `Serial`. `STX` (0x02) always starts a fresh frame, bytes outside a frame are ignored (boot noise), `ETX` (0x03) ends it. Oversize frames are dropped silently; an unfinished frame is abandoned after 500 ms.
2. **Frame check** (`handleFrame`): splits on the *last* `#`, verifies the 4-hex-digit CRC16-CCITT (poly 0x1021, init 0xFFFF) over the JSON bytes, parses the JSON, and requires a numeric `seq`. Failures reply `frame_error`, `crc_mismatch`, `bad_json` or `missing_seq` without echoing a seq.
3. **Session / sequencing** (`handleCommand`):
   - `hello` always succeeds, (re)starts the session at `seq + 1`, and clears the reply cache.
   - Before any `hello` since boot: `not_synced`.
   - Same `seq` as the last accepted command: the cached reply is resent verbatim (the desktop retried after a lost reply).
   - `seq` below the expected value: `replay_detected`.
   - Otherwise accepted; forward gaps are allowed.
4. **Policy** (`executeCommand`): op must be `set`/`get`/`erase`; key 1–15 chars; namespace must be `gnss`, `wifi` or `ntrip`; NTRIP lockout keys are `forbidden`; namespaces made immutable by build flags (`IMMUTABLE_UART/WIFI/NTRIP`) refuse writes with `read_only`.
5. **NVS** (`Preferences`, opened per command, read-only for `get`):
   - `set` only writes a key that already exists, with its stored type (`int`→I32, `uint`→U32, `bool`→U8, `string`→STR, max 256 bytes). No coercion: `"115200"` is not a `uint`. `float` is `unsupported_value`. The byte count returned by `put*` is checked (`nvs_write_failed`).
   - `get` picks the getter from the stored type; missing key → `not_found`.
   - `erase` is idempotent; `config_bootstrap()` re-seeds the namespace at next boot.
6. **Reply** (`buildFrame`): frames JSON as `STX + JSON + '#' + CRC + ETX`. Replies to accepted commands (ok *and* command errors) are built directly into the reply cache; `hello` and sequence/frame errors are not cached. A reply too large for 512 bytes is replaced by a short `unsupported_value` error.

## Design choices

- **Store only, apply nothing.** `onKeySet()` is an empty hook. `ntrip` changes are picked up by `configMonitorTask` (~5 s, when online); `gnss` and `wifi` take effect at next boot.
- **No key table.** The keys seeded by `config_bootstrap()` define what can be set, so the schema lives in one place.
- **Fixed memory.** Three static 512-byte buffers (RX, TX scratch, reply cache), no `String`, short-lived `JsonDocument`s.
- **Compiled out by default.** With `SERIAL_CONFIG_ENABLE=0`, the header is not included and the .cpp is empty.
