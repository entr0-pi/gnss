---
name: gnss-board-porting-advisor
description: Port this firmware to a different ESP32 board by updating PlatformIO target settings, pin/baud assumptions, and validation steps for serial bridge and optional features.
---

# GNSS Board Porting Advisor

Use this skill when switching to a new ESP32 board or module variant.

## Workflow

1. Capture target board details:
   - chip family / PlatformIO board id
   - UART-capable GPIOs
   - flash/filesystem constraints
2. Propose minimal `platformio.ini` deltas.
3. Align UART strategy:
   - hardcoded pins (locked) or runtime config (mutable)
4. Run verification sequence:
   - compile
   - boot/log sanity
   - UART passthrough
   - optional BLE/TCP/WebUI/NTRIP checks
5. Provide migration risks and mitigations.

## Output

- patch plan
- validation checklist
- rollback plan
