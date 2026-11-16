# Mass Build

Build the firmware for every target board in one command, using the
Docker export flow from [`build/`](../../build/README.md) and the retargeting
rules from [`utils/board/retarget.py`](../board/BOARD_PORTING.md).

## Prerequisites

- Python 3.9+
- Docker with BuildKit (Docker Desktop on Windows/macOS, or Docker Engine ≥ 23)

## Quick Start

```bash
# 1. List your boards in utils/mass-build/boards.json (see format below)

# 2. Preview the plan (nothing is modified, nothing runs)
python utils/mass-build/mass_build.py --dry-run

# 3. Build all boards
python utils/mass-build/mass_build.py
```

## How it Works

1. Builds the `gnss-base` image once (`build/Dockerfile.base`), unless it already exists.
2. Takes a snapshot of every file that `retarget.py` can modify.
3. For each board:
   - applies the `retarget.py` rules (board ID, chip, bootloader offset, labels)
   - `docker build -t gnss-mass:<name> -f build/Dockerfile .`
   - `docker run --rm -e CHIP=<chip> -v build/bin/<name>:/project/build gnss-mass:<name>`
   - checks that `gnss_<chip>.bin` was produced
4. Restores the snapshot, so your working tree ends up exactly as it was
   (also after a failure or Ctrl+C).
5. Prints a summary and exits non-zero if any board failed.

> Do not edit the files listed in [BOARD_PORTING.md](../board/BOARD_PORTING.md#what-the-script-updates)
> while a mass build is running: they are retargeted and then restored.

## Board List

### `boards.json` (default)

One reference build per chip that `build/Dockerfile.base` can build with the
default `platformio.ini` flags. Each entry uses the most widely used board for
its chip:

| NAME | Reference board | Chip | Bootloader |
|------|-----------------|------|------------|
| `esp32` | `esp32dev` | `esp32` | `0x1000` |
| `esp32s3` | `esp32-s3-devkitc-1` | `esp32s3` | `0x0` |
| `esp32c3` | `esp32-c3-devkitm-1` | `esp32c3` | `0x0` |

The ESP32-S2 is not listed. It has no Bluetooth, and the default `platformio.ini`
sets `-DBLE_ENABLE=1`, so its build fails in NimBLE-Arduino. None of the listed
chips uses the `0x2000` offset (see [Bootloader Offset](../board/BOARD_PORTING.md#bootloader-offset)).

> A firmware image only runs on the chip it was built for, which is why each
> chip has its own entry. Any board with the same chip and at least 4 MB of
> flash can run that chip's image. Boards that need different board-level
> settings, such as USB CDC on boot or the flash mode, need their own entry.

### `boards_all.json`

Every Arduino board with at least 4 MB of flash in `espressif32 @ 6.13.0`
(208 boards, chips `esp32`, `esp32s3` and `esp32c3`). ESP32-S2 boards are
excluded for the reason above.

```bash
python utils/mass-build/mass_build.py --boards utils/mass-build/boards_all.json
```

### Entry format

Entries use the same keys as the `.env` used by `retarget.py`, plus an optional `NAME`:

| Key | Required | Description |
|-----|----------|-------------|
| `NAME` | No | Output folder, image tag and name for `--only` (default: `TARGET_BOARD`) |
| `TARGET_BOARD` | Yes | Real PlatformIO board ID |
| `TARGET_CHIP` | Yes | Chip family, sets the bootloader offset |
| `TARGET_LABEL` | Yes | Human-readable name (web UI, docs) |
| `TARGET_GNSS` | Yes | GNSS module name (web UI subtitle) |

```json
{
  "boards": [
    {"NAME": "esp32",   "TARGET_BOARD": "esp32dev",           "TARGET_CHIP": "esp32",   "TARGET_LABEL": "ESP32 Dev Module",   "TARGET_GNSS": "MyGNSS"},
    {"NAME": "esp32s3", "TARGET_BOARD": "esp32-s3-devkitc-1", "TARGET_CHIP": "esp32s3", "TARGET_LABEL": "ESP32-S3 DevKitC-1", "TARGET_GNSS": "MyGNSS"},
    {"NAME": "esp32c3", "TARGET_BOARD": "esp32-c3-devkitm-1", "TARGET_CHIP": "esp32c3", "TARGET_LABEL": "ESP32-C3 DevKitM-1", "TARGET_GNSS": "MyGNSS"}
  ]
}
```

Or reuse existing `.env`-style files, one board per file:

```bash
python utils/mass-build/mass_build.py --env-files .env.example1 .env.example2
```

`TARGET_CHIP` must be `esp32`, `esp32s2`, `esp32s3` or `esp32c3`. An `esp32s2`
entry only builds with `BLE_ENABLE` turned off in `platformio.ini`. All entries
are checked before any build starts, so an unsupported chip stops the run with
no file changed.

## Options

| Option | Description |
|--------|-------------|
| `--boards FILE` | JSON board list (default: `utils/mass-build/boards.json`) |
| `--env-files F [F ...]` | Use `.env`-style files instead of JSON |
| `--only N [N ...]` | Build only these entries, by `NAME` (or `TARGET_BOARD` when no `NAME`), e.g. `--only esp32 esp32c3` |
| `--out DIR` | Output root (default: `build/bin`) |
| `--rebuild-base` | Rebuild `gnss-base` even if it exists |
| `--no-cache` | Pass `--no-cache` to each per-board `docker build` |
| `--prune-images` | Remove each `gnss-mass:<board>` image after export |
| `--fail-fast` | Stop at the first failing board |
| `--dry-run` | Print plan and commands only |

## Output

```
build/bin/
├── esp32/
│   ├── bootloader.bin  partitions.bin  firmware.bin  data.bin
│   ├── gnss_esp32.bin            # merged image
│   ├── flash.sh  flash.ps1  flash.cmd   # bootloader written at 0x1000
│   ├── board.json                # board definition used for this build
│   └── build.log                 # full docker build + run log
├── esp32s3/                      # gnss_esp32s3.bin, bootloader at 0x0
└── esp32c3/                      # gnss_esp32c3.bin, bootloader at 0x0
```

The merged image already contains the bootloader at the right offset, so you
always flash it at `0x0`:

```bash
python -m esptool --chip esp32 --port <PORT> write-flash 0x0 build/bin/esp32/gnss_esp32.bin
```

The separate `.bin` files must be written at their own offsets. The `flash.*`
scripts in each folder already use the right bootloader offset.

## Notes

- `gnss-base` pre-installs only the RISC-V toolchain, so `esp32c3` builds
  offline. The Xtensa chips (`esp32`, `esp32s3`) download their
  toolchain inside each per-board `docker build`. This works, but it is slower
  and needs network access.
- `partitions.csv` is shared by all boards and is not retargeted. Boards with
  less flash than the partition table expects will fail at build or flash time.
