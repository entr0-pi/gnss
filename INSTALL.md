# Installation & Quickstart

## QuickStart

### A) Flashing A Prebuilt Binary

Use this when you want the fastest bring-up without building locally.

What you need:
- A GNSS (U-blox, Unicorn...). I used the UM980
- A USB cable for flashing and powering the ESP32 board
- An ESP32 board with minimum 4M flash memory
- A prebuilt binary set (`bootloader.bin`, `partitions.bin`, `firmware.bin`, `data.bin`) provided in releases
- A flash tool (`esptool.py`, PlatformIO upload, or Espressif Flash Download Tool)

Typical offsets for this repo partition table (see *partitions.csv* ):
- `bootloader.bin` -> `0x1000`
- `partitions.bin` -> `0x8000`
- `firmware.bin` -> `0x10000`
- `data.bin` -> `0x310000`

<img src="assets/esp32-flash.png" alt="Web UI 3" width="250">

After flashing, the firmware immediately:
- boots,
- seeds NVS defaults when mutable,
- starts enabled features from build flags.

Important default behavior:
- UART defaults to unconfigured (`rx_pin=-1`, `tx_pin=-1`, `baud=0`) unless hardcoded at build time
- If you want to parse the NMEA in the webUI (and see the Skyplot...), the GNSS must output NMEA sentences to the ESP32's UART (configure the RX/TX/BAUD in the webUI)
- In that unconfigured state, UART bridging is intentionally skipped until configured.
- Connect to the WiFi Access Point "GNSS-ESP32-AP", then go to http://192.168.4.1 (unless you used a different set of flags)
- After connecting, configure your GNSS UART settings, WiFi credentials, and NTRIP server details through the web interface (click on the gear)

**Preload NVS settings (recommended for field deployment):**
- Use `utils/uploader/uploaderGUI.py` to write GNSS/WiFi/NTRIP values to NVS and flash LittleFS web assets.
<img src="assets/Uploader-2.png" alt="Web UI 3" width="250">
- This avoids recompiling just to change target wiring or network values.

### B) Building From Source

Use this when you need to:
- change feature flags,
- use another partition,
- modify firmware behavior.

Prerequisites:
- Docker installed

**Build with Docker:** 
- change the flags in *platformio.ini*
- adapt the partition in *partition.csv*
- any other modifications in the codebase ***at your own risk***
- see README in /build (full automation in a docker container). Output in /bin for the binaries and the flash commands

### C) Utils

Available tooling under `utils/`:
- `utils/uploader/uploaderGUI.py`: NVS editor + writer, LittleFS builder/flasher
- `utils/render-web/render_web.py`: local dev server for `data/web/` with mock API
- `utils/build-tester/build_Tester.py`: build matrix tester for feature flags

Utils goals and when to use them:

1. `utils/uploader/` (deployment and field configuration)
- Goal: configure devices without rebuilding firmware.
- Use it to prepare and flash:
  - NVS key/values (GNSS UART, WiFi, NTRIP),
  - LittleFS image (Web UI assets).
- Best for: production setup, installer workflow, lab technicians.
- Expected outcome: device is flashed with firmware-compatible config and web assets in one workflow.

2. `utils/render-web/` (Web UI development loop)
- Goal: iterate quickly on Web UI outside embedded target constraints.
- Serves `data/web/` with mocked API endpoints that mimic firmware routes.
- Best for: frontend work, API contract checks, UI regression checks before flashing.
- Expected outcome: faster UI iteration with no board required for every change.

3. `utils/build-tester/` (compile-time flag validation)
- Goal: verify feature-flag combinations still compile as the project evolves.
- Runs tiered build matrices, including expected-failure guard tests.
- Best for: CI validation, pre-release sanity checks, refactor safety.
- Expected outcome: early detection of broken flag combinations and guard regressions.

Recommended workflow:
- Develop and validate UI behavior with `render-web`
- Validate firmware flag combinations with `build-tester`
- Produce deployable config + assets with `uploader`
