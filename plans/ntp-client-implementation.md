# NTP Client Implementation Plan (when internet is reachable)

## Goal
Implement an NTP synchronization client that updates device time only when the network path to the internet is reachable, with safe retry behavior and observability in the Web UI.

## 1) Add feature flag and module skeleton
1. Add `NTP_CLIENT_ENABLE` in `include/app.h` (default `0`).
2. Create `include/ntp_client.h` with:
   - `void ntp_client_setup();`
   - `void ntp_client_loop();`
   - optional getters for status snapshots.
3. Create `src/ntp_client.cpp` with internal state, timers, and logging.

## 2) Add NTP configuration model
1. Add `include/config_ntp.h` and `src/config_ntp.cpp`.
2. Define `NtpConfig` fields:
   - `enabled` (bool)
   - `server` (String, default `pool.ntp.org`)
   - `tz` or `tz_env` (String)
   - `sync_interval_ms` (uint32_t)
   - `sync_timeout_ms` (uint32_t)
   - `max_tries` (int)
   - `retry_delay_ms` (uint32_t)
3. Implement `*_defaults`, `*_validate`, `*_load`, `*_save` APIs similar to other config modules.

## 3) Extend NVS schema and bootstrap
1. Add `nvs_keys::ntp::*` entries in `include/nvs_keys.h`.
2. Increment `NVS_SCHEMA_VERSION` and add/update required key count macro(s).
3. Update `src/config_bootstrap.cpp`:
   - add `ntp_nvs_has_data()` check
   - seed NTP defaults when data is missing and config is mutable.
4. Add static assertions for NTP schema/key count in `src/config_ntp.cpp`.

## 4) Internet reachability gate for sync attempts
1. Add an internal helper in `ntp_client.cpp`:
   - if `WEBUI_ENABLE`: `webui_get_internet_reachable() || (WiFi.status() == WL_CONNECTED)`
   - else: `WiFi.status() == WL_CONNECTED`
2. Use this helper to prevent NTP sync attempts when internet is not reachable.
3. Keep behavior robust even if `/api/status` has never been polled.

## 5) Implement NTP state machine
Implement a lightweight non-blocking state model:
- `IDLE` (disabled or waiting for interval)
- `WAIT_NET` (WiFi/internet unavailable)
- `SYNCING` (attempt in progress)
- `SYNCED` (last sync successful)
- `BACKOFF` (failed attempt, wait before retry)

Rules:
1. Initial fast sync after boot when time is invalid.
2. Periodic sync according to `sync_interval_ms` when stable.
3. Retry with bounded backoff on failures.
4. Reset failure counters after a successful sync.

## 6) Hook into ESP32 SNTP APIs
1. Configure timezone via `setenv("TZ", ...)` and `tzset()`.
2. Configure SNTP server via `configTime(...)` / `configTzTime(...)`.
3. Validate sync result with `getLocalTime(...)` and sanity checks on `tm`.
4. Store:
   - last successful sync timestamp
   - last sync age
   - consecutive failure count
   - last error/status text.

## 7) Integrate into app lifecycle
1. In `setup()` call `ntp_client_setup()` under `#if NTP_CLIENT_ENABLE`.
2. In `loop()` call `ntp_client_loop()` under the same flag.
3. Ensure call ordering does not block existing subsystems.

## 8) Web UI status exposure
1. Add NTP fields to `/api/status` output, e.g.:
   - `enabled`
   - `state`
   - `time_valid`
   - `last_sync_epoch`
   - `last_sync_age_ms`
   - `consecutive_failures`
2. Optionally add `/api/config/ntp` GET/POST endpoints to manage NTP config.

## 9) Logging and failure diagnostics
1. Log transitions (`WAIT_NET`, `SYNCING`, `BACKOFF`, `SYNCED`).
2. Log NTP server and timeout used for each attempt.
3. Log failure reason categories (DNS fail, timeout, invalid time, etc.).

## 10) Validation checklist
1. WiFi disconnected -> stays in `WAIT_NET`, no blocking.
2. WiFi connected but internet blocked -> retries/backoff works.
3. Internet reachable -> successful sync updates status.
4. Web UI not opened -> fallback gate still allows sync when link is up.
5. Reboot persistence -> NTP config loads from NVS.
6. `/api/status` reflects changing sync state correctly.

## 11) Rollout order
1. `config_ntp` + NVS keys + bootstrap.
2. `ntp_client` core state machine + SNTP integration.
3. `main.cpp` lifecycle wiring.
4. Web UI status (and optional config endpoints).
5. End-to-end validation and log tuning.
