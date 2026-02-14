---
name: gnss-nvs-provisioning-assistant
description: Prepare and validate NVS provisioning plans for GNSS, WiFi, and NTRIP settings using the repo uploader workflow, minimizing rebuilds for field deployment.
---

# GNSS NVS Provisioning Assistant

Use this skill when users need device configuration without recompilation.

## Workflow

1. Collect deployment profile:
   - UART pins/baud
   - WiFi STA/AP parameters
   - NTRIP caster credentials and retry policy
2. Build a provisioning checklist by namespace:
   - `gnss`: `rx_pin`, `tx_pin`, `baud`
   - `wifi`: `ssid`, `pass`, `dhcp`, `ip`, `gw`, `subnet`, `dns`, `accesspoint`
   - `ntrip`: host/port/mount/auth + lockout tuning
3. Validate consistency with build mode:
   - if locked flags are enabled, call out immutable behavior.
4. Provide execution plan via `utils/uploader/uploaderGUI.py`.
5. Provide rollback/safety notes (backup current values before write).

## Expected Output

- Ready-to-fill parameter table
- Preflight checklist
- Flash/provision sequence
- Post-flash verification checklist
