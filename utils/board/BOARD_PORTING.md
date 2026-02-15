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
- `utils/uploader/uploaderGUI.py` — default chip selection
- `build/scripts/docker-export-bins.sh` — chip defaults and bootloader offset in flash helpers
- `data/web/index.html` — page title and subtitle
- `data/web/app.js` — restart confirmation dialog

**Documentation files:**
- `INSTALL.md` — bootloader flash offset
- `docs/DIAGRAM.md` — description, Mermaid subgraph label, board line
- `utils/build-tester/README.md` — title and description
- `utils/build-tester/build_Tester.py` — docstrings and help text

**Not updated (by design):**
- `src/main.cpp` code comments (pin documentation, chip-specific notes)
- `partitions.csv` (may need manual adjustment for different flash sizes)

## Bootloader Offset

The script automatically sets the correct bootloader flash offset based on the chip family:

| Chip | Bootloader Offset |
|------|-------------------|
| ESP32 | `0x1000` |
| ESP32-S2 | `0x1000` |
| ESP32-S3, C2, C3, C6, H2, P4 | `0x0` |

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

## CLI Options

```
python utils/board/retarget.py [OPTIONS]

  --dry-run     Preview changes without writing to disk
  --env-file    Path to .env file relative to project root (default: .env)
```
