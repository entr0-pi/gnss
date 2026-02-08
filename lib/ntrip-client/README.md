# ntrip-client

Embedded NTRIP client for ESP32/Arduino targets.

It connects to an NTRIP caster, validates RTCM stream quality, forwards corrections to a `Print` output (typically GNSS UART), and handles reconnect/lockout logic.

## Features

- NTRIP Rev2 with automatic fallback to Rev1
- Two-phase stream validation:
  - strict RTCM frame validation at startup
  - passive preamble sampling during steady state
- Zombie stream detection (`healthTimeoutMs`)
- Lockout after repeated failures (`maxTries`)
- Runtime stats and error reporting
- FreeRTOS task loop ready for ESP32
- Output abstraction via `Print` (UART, stream buffer writer, etc.)
- Logger abstraction via callback injection (no hard dependency on app logger)

## Public API

Main types in `include/NtripClient.h`:

- `NtripConfig`: caster and behavior settings
- `NtripState`: `DISCONNECTED | CONNECTING | STREAMING | LOCKED_OUT`
- `NtripError`: detailed failure categories
- `NtripStats`: counters + last error/frame info
- `NtripClient`: client class

Important methods:

- `begin(const NtripConfig&, Print&)`
- `startTask(uint8_t core = 0)`
- `stop()`, `reset()`, `reconnect()`
- `state()`, `isStreaming()`, `isHealthy()`
- `getStats()`, `getLastError()`, `getErrorMessage()`
- `setLogger(NtripLogFn logger)`

## Logger Abstraction

The library is logger-agnostic.

Define a callback in your app and inject it:

```cpp
static void ntripLog(NtripLogLevel level, const char* tag, const char* message) {
  // Route to your logger implementation
}

NtripClient client;
client.setLogger(ntripLog);
```

If `setLogger()` is not called, library logs are silent.

### Callback signature

```cpp
using NtripLogFn = void (*)(NtripLogLevel level, const char* tag, const char* message);
```

`NtripLogLevel` values:

- `Error`
- `Warning`
- `Info`
- `Debug`

## Configuration Notes

`NtripConfig` fields:

- `host`, `port`, `mount`, `user`, `pass`
- `maxTries`, `retryDelayMs`
- `healthTimeoutMs`, `passiveSampleMs`
- `requiredValidFrames`
- `bufferSize`
- `connectTimeoutMs`

Tuning tradeoffs:

- Lower `retryDelayMs`: faster recovery, more network/caster load
- Lower `healthTimeoutMs`: faster zombie detection, less tolerance to sparse streams
- Higher `requiredValidFrames`: safer validation, slower startup
- Higher `bufferSize`: handles bursts better, uses more RAM

## Integration Pattern (Project)

Typical pattern used in this repository:

1. Create a `Print` bridge writer to your StreamBuffer/UART path.
2. Build/load `NtripConfig`.
3. Call `client.setLogger(...)` with app logger adapter.
4. Call `client.begin(config, writer)`.
5. Start task with `client.startTask(core)`.
6. Monitor with `state()` / `getStats()`.

## Examples

See `examples/` for usage patterns:

- `basic`
- `advanced`
- `minimal`
- `production`
