# Main Refactoring Plan (Safe, Progressive)

This guide breaks the `src/main.cpp` refactor into small, reversible steps. Each step isolates a distinct phase of `setup()` or `loop()`, keeping behavior identical. Apply one step at a time, compile/test, then move to the next.

> **Goal:** Make `main.cpp` leaner by extracting self-contained helper functions and minimizing logic in `setup()`/`loop()`.

---

## Step 0 — Baseline & Safety Prep

**Objective:** Capture the current state and ensure a known-good baseline before changing structure.

1. **Confirm current behavior**
   - Build/flash or run the existing build command for this repo/environment.
2. **Record baseline output**
   - Capture the first few serial logs from boot (`[SETUP] ...`, `[LOOP] ...`).
3. **Create a working branch**
   - Start a new branch for the refactor.

**Testing/checks:**
- Build/flash or compile-only command.
- Smoke check that boot logs appear as expected.

---

## Step 1 — Extract Serial + Config Bootstrapping

**Objective:** Move initial “boot” lines into a single helper.

**Action:**
- Create a new function (example name: `initSerialAndConfig()`).
- Move these lines out of `setup()` into the helper:
  - `Serial.begin(...)`
  - `vTaskDelay(...)`
  - `gnss_config_begin()`
- Replace original lines in `setup()` with a single call.

**Rationale:**
- This is a purely mechanical move that should not change runtime behavior.

**Testing/checks:**
- Rebuild and confirm boot logs are unchanged.

---

## Step 2 — Extract Stream Buffer Creation & Validation

**Objective:** Isolate StreamBuffer allocation and failure handling.

**Action:**
- Create `createStreamBuffers()`.
- Move the `xStreamBufferCreateStatic(...)` calls into the helper.
- Keep the allocation failure check (and infinite delay loop) inside the helper.
- Replace block in `setup()` with `createStreamBuffers()`.

**Rationale:**
- Stream buffer creation is a cohesive block and a good low-risk extraction.

**Testing/checks:**
- Rebuild and confirm no changes to logs or behavior.

---

## Step 3 — Extract UART Conditional Setup

**Objective:** Keep UART configuration logic together and explicit.

**Action:**
- Create `setupUartIfConfigured()`.
- Move `gnss_config_get()` + pin/baud check + `setupUART()` call inside the helper.
- Replace the block in `setup()`.

**Rationale:**
- Keeps “policy” (skip UART if not configured) separate from the UART setup implementation.

**Testing/checks:**
- Rebuild and verify UART behavior is unchanged.

---

## Step 4 — Extract BLE Initialization

**Objective:** Make BLE startup a single line in `setup()`.

**Action:**
- Create `startBleServer()` that logs and calls `setupBLE()`.
- Replace the BLE block in `setup()` with this call.

**Testing/checks:**
- Rebuild and verify BLE advertises and connects as before.

---

## Step 5 — Extract Web UI & Wi‑Fi Phases

**Objective:** Separate web UI route setup, Wi‑Fi connection, and server start.

**Action:**
- Create these helpers (guarded by the same compile flags):
  - `initWebUiRoutes()` (only if `WEBUI_ENABLE`)
  - `connectWiFi()` (only if `WIFI_ENABLE`, calling `setupWiFi()`)
  - `startWebServer()` (only if `WEBUI_ENABLE`, calling `server.begin()`)
- Replace each section in `setup()` with the corresponding helper.

**Testing/checks:**
- Rebuild and confirm Wi‑Fi connects and web UI responds.

---

## Step 6 — Extract NMEA Initialization

**Objective:** Make NMEA startup a single function call.

**Action:**
- Create `initNmea()` (guarded by `NMEA_ENABLE`).
- Move `nmea_begin()` and its log line into the helper.
- Replace the block in `setup()`.

**Testing/checks:**
- Rebuild and confirm NMEA processing still occurs.

---

## Step 7 — Extract Task Creation

**Objective:** Isolate task creation (and TCP server start) into a helper.

**Action:**
- Create `startWorkerTasks()`.
- Move all `xTaskCreate(...)` calls (and the TCP server start if enabled) into the helper.
- Replace the block in `setup()` with `startWorkerTasks()`.

**Testing/checks:**
- Rebuild and verify all tasks run (BLE notify, UART RX/TX, TCP if enabled).

---

## Step 8 — Simplify `loop()` With Small Helpers

**Objective:** Keep `loop()` as a sequence of clear actions.

**Action:**
- Create helper functions:
  - `logLoopEntryOnce()` (first loop banner)
  - `maybeReconnectWiFi()` (Wi‑Fi reconnect logic)
  - `handleWebUi()` (web server polling)
  - `yieldToTasks()` (small delay)
- Replace the `loop()` body with calls to those helpers.

**Testing/checks:**
- Rebuild and verify the loop continues to serve web UI and reconnect Wi‑Fi.

---

## Step 9 — Clean Up Declarations & Ordering

**Objective:** Ensure helper declarations and definitions are clear and consistent.

**Action:**
- Add forward declarations for new helpers near the existing function declarations.
- Keep helper definitions grouped in the “FUNCTIONS” section.
- If possible, order helpers in a logical sequence (setup-related, loop-related).

**Testing/checks:**
- Rebuild to ensure no missing declarations or ordering issues.

---

## Step 10 — Final Review & Consolidation

**Objective:** Confirm no behavior changes and finalize the refactor.

**Action:**
- Compare boot logs and functional behavior to the baseline.
- Confirm compile-time flags (`WIFI_ENABLE`, `WEBUI_ENABLE`, `NMEA_ENABLE`, `TCP_ENABLE`) still gate the same blocks.
- If desired, add brief doc comments for the new helpers.

**Testing/checks:**
- Full build/flash and an end-to-end smoke test (BLE, UART, Wi‑Fi, Web UI).

---

## Notes for AI Agents

- **Do one step per commit** whenever possible.
- **Avoid behavior changes** (no logic edits, only moving code).
- **Keep compile guards intact** around each extracted helper.
- **Prefer short, descriptive names** for helpers that map clearly to the original blocks.

---

## Suggested Helper Names (Reference)

- `initSerialAndConfig()`
- `createStreamBuffers()`
- `setupUartIfConfigured()`
- `startBleServer()`
- `initWebUiRoutes()`
- `connectWiFi()`
- `startWebServer()`
- `initNmea()`
- `startWorkerTasks()`
- `logLoopEntryOnce()`
- `maybeReconnectWiFi()`
- `handleWebUi()`
- `yieldToTasks()`
