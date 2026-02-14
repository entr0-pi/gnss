---
name: gnss-build-flags-planner
description: Plan and generate PlatformIO build_flags for this GNSS ESP32 firmware based on target mode (lab, field, production-locked), feature requirements (BLE/WebUI/TCP/NMEA/NTRIP), and project constraints between flags.
---

# GNSS Build Flags Planner

Use this skill when the user asks for a recommended flag set, asks "which flags should I use", or needs a copy-paste `build_flags` block.

## Workflow

1. Identify deployment intent: `lab`, `field`, or `production-locked`.
2. Map requested capabilities to feature flags:
   - `BLE_ENABLE`
   - `WEBUI_ENABLE`
   - `TCP_ENABLE`
   - `NMEA_ENABLE`
   - `NTRIP_CLIENT_ENABLE`
3. Enforce constraints:
   - If `NTRIP_CLIENT_ENABLE=1`, force `WEBUI_ENABLE=1`.
   - If `NMEA_ENABLE=1` without Web UI, explain it is ineffective and align with `WEBUI_ENABLE=1`.
4. Add optional lock flags when requested:
   - `FORCE_HARDCODED_UART` (+ `HARD_RX_PIN`, `HARD_TX_PIN`, `HARD_BAUD`)
   - `FORCE_WIFI_SECRETS`
5. Output:
   - final `build_flags` block
   - short rationale per flag
   - quick validation commands (`pio run`, optionally build-tester tiers)

## Output Template

```ini
build_flags =
  -DWEBUI_ENABLE=1
  -DNMEA_ENABLE=1
  -DTCP_ENABLE=1
  -DTCP_PORT=52148
  -DNTRIP_CLIENT_ENABLE=1
```

Then include "Why this configuration" bullets and mention tradeoffs.
