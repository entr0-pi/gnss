# External Integrator Checklist

Use this checklist when adapting this project to your own hardware/network/deployment.

## 1) Hardware / Board Target (must review)

- [ ] **Set your PlatformIO board** (`platformio.ini` currently uses `lolin_c3_mini`).
- [ ] **Confirm chip family** for flashing tools (`esp32c3` defaults in helper scripts).
- [ ] **Adjust UART wiring and pin mapping** to match your GNSS module and board.
- [ ] **Review ESP32-C3-specific assumptions** in docs/UI text if you are using another chip/board.

## 2) Flashing Parameters (must review)

- [ ] **Set serial port** for your host OS (`/dev/ttyUSB0`, `COM8` are examples/defaults).
- [ ] **Validate flash offsets** for your build/partition layout before using prebuilt bins.
- [ ] **Verify partition table size/layout** (`partitions.csv`) for your module flash capacity.

## 3) Runtime GNSS/UART Configuration (must review)

- [ ] Choose operation mode:
  - **Runtime-configurable** via Web UI + LittleFS, or
  - **Compile-time locked** via `FORCE_HARDCODED_UART` + `HARD_RX_PIN/HARD_TX_PIN/HARD_BAUD`.
- [ ] If using compile-time lock, set **real pins/baud** for your hardware.
- [ ] If using runtime mode, preload/verify `/gnss.json` values and ranges for your board.
- [ ] Re-check fallback values (`FALLBACK_GNSS_RX/TX/BAUD`) for your hardware safety path.

## 4) Wi-Fi / Network Configuration (must review)

- [ ] Replace Wi-Fi placeholders in `include/secrets.h` (copied from `secrets.example.h`) if using forced secrets.
- [ ] Replace default placeholder credentials (`CHANGE_ME`, `your-ssid`, `your-password`) before deployment.
- [ ] Confirm DHCP vs static IP settings are valid for your target LAN.

## 5) NTRIP Configuration (if enabled)

- [ ] Set real caster settings: **host, port, mountpoint, username, password**.
- [ ] Replace example values (`rtk2go.com`, `YOUR_MOUNT`, sample email/user/pass).
- [ ] Keep valid retry/timeout/buffer values for your network conditions.
- [ ] Verify NTRIP is enabled only when required dependencies (`WEBUI_ENABLE`, Wi-Fi path) are configured.

## 6) Product Identity / UX Text (recommended)

- [ ] Update BLE name (`BLE_DEVICE_NAME`) from default/project examples.
- [ ] Update app/device naming strings if needed (for branding/fleet differentiation).
- [ ] Update Web UI copy that currently says **ESP32-C3** / **LOLIN** if your hardware differs.

## 7) Build Flags and Features (recommended)

- [ ] Review feature flags for your scenario:
  - `WEBUI_ENABLE`, `BLE_ENABLE`, `TCP_ENABLE`, `NMEA_ENABLE`, `NTRIP_CLIENT_ENABLE`.
- [ ] Set `TCP_PORT` and BLE MTU/rate settings as needed by your client apps.
- [ ] Rebuild and retest after each configuration change.

## 8) Security / Production Hardening (recommended)

- [ ] Avoid committing real credentials (`secrets.h`, Wi-Fi/NTRIP auth values).
- [ ] Keep release artifacts separated from environment-specific config files.
- [ ] Validate lock/read-only behavior if using compile-time enforced configs.

## 9) Final Validation Before Field Use

- [ ] Flash firmware + filesystem/NVS data with your final settings.
- [ ] Verify UART RX/TX data flow with your GNSS device.
- [ ] Verify BLE streaming and/or TCP streaming on your client tooling.
- [ ] Verify Web UI status updates and config persistence across reboot.
- [ ] If using NTRIP, verify stable correction stream and lockout/retry behavior.

---

## Quick Reference of Project-Specific Defaults to Replace

- Board/chip defaults: `lolin_c3_mini`, `esp32c3`.
- Serial port examples: `/dev/ttyUSB0`, `COM8`.
- UART example pins: GPIO20/GPIO21 and example bauds.
- Wi-Fi placeholders: `YOUR_SSID`, `YOUR_PASSWORD`, `CHANGE_ME`, `your-ssid`, `your-password`.
- NTRIP placeholders/examples: `rtk2go.com`, `YOUR_MOUNT`, sample user/pass.
- UX text mentions: `ESP32-C3` / `LOLIN`.
