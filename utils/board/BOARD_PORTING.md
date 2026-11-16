# Board Porting Guide

Retarget this firmware to a different ESP32 board in one command.

## Prerequisites

- Python 3.6+
- A `.env` file at the project root (copy from `.env.example`)

## Quick Start

```bash
# 1. Copy the template
cp .env.example .env

# 2. Edit .env with your target board
#    (see Variable Reference below)

# 3. Preview changes
python utils/board/retarget.py --dry-run

# 4. Apply
python utils/board/retarget.py
```

## Variable Reference

| Variable | Description | Example |
|----------|-------------|---------|
| `TARGET_BOARD` | PlatformIO board ID | `lolin_c3_mini`, `esp32-s3-devkitc-1` |
| `TARGET_CHIP` | Chip family (for esptool) | `esp32c3`, `esp32s3`, `esp32` |
| `TARGET_LABEL` | Human-readable name (docs, web UI) | `ESP32-C3 LOLIN MINI` |
| `TARGET_GNSS` | GNSS module name (web UI subtitle) | `UM980`, `ZED-F9P` |

Browse available boards at: https://registry.platformio.org/platforms/platformio/espressif32/boards

## What the Script Updates

**Functional files:**
- `platformio.ini` — board ID
- `utils/build-tester/platformio.ini.test` — env name and board ID
- `build/scripts/docker-export-bins.sh`: default chip and bootloader offset in the merged image
- `build/scripts/bin_utils/flash.sh`, `flash.ps1`, `flash.cmd`: default chip and bootloader offset
- `data/web/index.html`: page title and subtitle
- `data/web/app.js`: restart confirmation dialog

**Documentation files:**
- `INSTALL.md`: bootloader flash offset
- `utils/build-tester/README.md`: title and description

Line endings are preserved (LF for `.sh`, as `.gitattributes` requires).

**Not updated (by design):**
- `src/main.cpp` code comments (pin documentation, chip-specific notes)
- `partitions.csv` (may need manual adjustment for different flash sizes)

## Bootloader Offset

The script automatically sets the correct bootloader flash offset based on the chip family:

| Chip Family | Bootloader Offset | Buildable with  |
|-------------|-------------------|----------------------------------------|
| ESP32, ESP32-S2 | `0x1000` | ESP32, ESP32-S2 |
| ESP32-S3, ESP32-C2, ESP32-C3, ESP32-C6, ESP32-C61, ESP32-H2 | `0x0` | ESP32-S3, ESP32-C3 |
| ESP32-C5, ESP32-P4, ESP32-H4, ESP32-S31 | `0x2000` | None |

Valid `TARGET_CHIP` values: `esp32`, `esp32s2`, `esp32s3`, `esp32c3`.

These are the only chips with Arduino support in the stack pinned by
`build/Dockerfile.base` (`espressif32 @ 6.13.0`, Arduino core 2.0.17). The
other chips in the table need Arduino core 3.x, and the script rejects them
with an explicit error.

The ESP32-S2 has no Bluetooth. Retarget to `esp32s2` only after removing
`-DBLE_ENABLE=1` from `platformio.ini`, otherwise the build fails in NimBLE-Arduino.

## Example: Retarget to ESP32-S3 DevKitC-1

```ini
# .env
TARGET_BOARD=esp32-s3-devkitc-1
TARGET_CHIP=esp32s3
TARGET_LABEL=ESP32-S3 DevKitC-1
TARGET_GNSS=UM980
```

```bash
python utils/board/retarget.py --dry-run   # review
python utils/board/retarget.py             # apply
pio run -e full                            # verify build
```

## After Retargeting

1. **Partition table** — Review `partitions.csv` if the new board has different flash size.
2. **UART pins** — The default GNSS RX/TX pins are configured at runtime via NVS or the web UI. If using `FORCE_HARDCODED_UART`, update the pin numbers in `platformio.ini` build flags.
3. **Build & test** — Verify compilation for the new board.

## Building Several Boards

To build one firmware per supported chip (`esp32`, `esp32s3`, `esp32c3`) or a whole board list in one run, use
[`utils/mass-build/`](../mass-build/README.md). It applies these retarget rules
for each entry, builds with Docker, and restores the working tree afterwards.

## CLI Options

```
python utils/board/retarget.py [OPTIONS]

  --dry-run     Preview changes without writing to disk
  --env-file    Path to .env file relative to project root (default: .env)
```
