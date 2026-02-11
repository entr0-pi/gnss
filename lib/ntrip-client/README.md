# ntrip-client

Embedded NTRIP client library for ESP32/Arduino targets.

Connects to an NTRIP caster, validates RTCM stream quality, forwards corrections to a `Print` output (typically GNSS UART), and handles reconnect/lockout logic.

## Features

- NTRIP Rev2 with automatic fallback to Rev1 (compile-time toggle)
- Two-phase stream validation: strict RTCM parsing at startup, passive preamble sampling at steady state
- Zombie stream detection
- Lockout after repeated failures with reset/reconnect API
- Config validation at `begin()` with actionable error messages
- Runtime stats with batched updates (low mutex contention)
- Task lifecycle control: `startTask()` / `stopTask()` with duplicate-spawn protection
- Output abstraction via `Print`
- Logger callback injection (silent by default)

## Thread-safety contract

- `begin()`, `startTask()`, `stopTask()` must be called from the same context (typically Arduino setup/loop). Never call `begin()` while the task is running.
- Query methods — `state()`, `isStreaming()`, `isHealthy()`, `getStats()`, `getLastError()`, `getErrorMessage()` — are safe from any task.
- Control methods — `stop()`, `reset()`, `reconnect()` — are safe from any task.
- Scalar state uses `volatile` for lock-free single-writer access. `NtripStats` is protected by mutex.

## Compile-time flags

Override via build_flags (e.g. `-DNTRIP_CLIENT_ENABLE_TASK=0`):

| Flag | Default | Description |
|------|---------|-------------|
| `NTRIP_CLIENT_ENABLE_TASK` | `1` | Enable FreeRTOS background task. Set to `0` for non-RTOS targets. |
| `NTRIP_CLIENT_ENABLE_REV1_FALLBACK` | `1` | Automatic Rev2 → Rev1 fallback on connection failure. |
| `NTRIP_CLIENT_PASSIVE_SCAN_BYTES` | `128` | Bytes scanned for RTCM preamble during passive health checks. |

## Public API

Types in `include/NtripClient.h`:

- `NtripClientConfig` — caster and behavior settings
- `NtripState` — `DISCONNECTED | CONNECTING | STREAMING | LOCKED_OUT`
- `NtripError` — failure categories (includes `INVALID_CONFIG`)
- `NtripStats` — counters + last error/frame info
- `NtripClient` — client class

Methods:

| Method | Description |
|--------|-------------|
| `begin(cfg, gnss)` | Initialize with config and output stream. Validates config. |
| `startTask(core)` | Start FreeRTOS task. Returns `false` if already running. |
| `stopTask()` | Signal task to stop, wait for clean exit, free resources. |
| `isTaskRunning()` | Check if background task is active. |
| `stop()` | Enter lockout state. |
| `reset()` | Clear lockout, allow reconnection. |
| `reconnect()` | Force immediate reconnection attempt. |
| `state()` | Current `NtripState`. |
| `isStreaming()` | True if actively streaming. |
| `isHealthy()` | True if validated and receiving data. |
| `getStats()` | Thread-safe stats snapshot. |
| `getLastError()` | Last `NtripError` code. |
| `getErrorMessage()` | Human-readable error string. |
| `setLogger(fn)` | Inject log callback. Silent if unset. |
| `validateConfig(cfg, err)` | Static config validation. |

## Logger

The library is logger-agnostic. Inject a callback to receive log messages:

```cpp
static void ntripLog(NtripLogLevel level, const char* tag, const char* message) {
  Serial.printf("[%s] %s\n", tag, message);
}

ntrip.setLogger(ntripLog);
```

If `setLogger()` is not called, the library is silent.

## Configuration

`NtripClientConfig` fields:

| Field | Default | Notes |
|-------|---------|-------|
| `host`, `port`, `mount`, `user`, `pass` | — | Required (validated at `begin()`) |
| `ggaSentence` | `""` | Optional GGA for Rev2 Ntrip-GGA header |
| `maxTries` | `5` | Attempts before lockout |
| `retryDelayMs` | `30000` | Lower = faster recovery, more load |
| `healthTimeoutMs` | `60000` | Lower = faster zombie detection |
| `passiveSampleMs` | `5000` | Passive health check interval |
| `requiredValidFrames` | `3` | Higher = safer validation, slower start |
| `bufferSize` | `1024` | Higher = handles bursts, uses more RAM |
| `connectTimeoutMs` | `5000` | TCP + HTTP response timeout |

## Integration pattern

```cpp
NtripClient ntrip;
ntrip.setLogger(myLogCallback);

NtripClientConfig cfg;
cfg.host = "rtk2go.com";
cfg.mount = "MY_MOUNT";
// ... fill remaining fields ...

if (!ntrip.begin(cfg, Serial2)) {
  // handle error
}
ntrip.startTask(0);

// Monitor from loop:
// ntrip.state(), ntrip.isHealthy(), ntrip.getStats()
```

## Examples

- `minimal` — bare minimum to get corrections flowing
- `basic` — self-contained with WiFi, logger, stats, and error recovery
