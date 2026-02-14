# ESP32 LittleFS + NVS Uploader GUI

Desktop GUI utility for preparing and flashing LittleFS content and NVS partition data to ESP devices.

## What this tool does

`uploaderGUI.py` provides a Tkinter app with tabs for:

- **Configuration** (serial/chip/tool paths/partition CSV)
- **Flash Operations** (build + flash LittleFS image)
- **NVS Editor** (edit key/value entries, import/export CSV, write/erase NVS)
- **Terminal Output** (live command logs)
- **System Setup** (dependency guidance)

It orchestrates tools like `mklittlefs`, `esptool`, and `nvs_partition_gen.py` for a workflow that would otherwise require many manual shell commands.

## How to use

From the repo root:

```bash
python utils/uploader/uploaderGUI.py
```

### Required Python packages

```bash
pip install esptool sv-ttk
```

### Typical workflow

1. Set serial port, chip family, partition CSV, and tool paths in **Configuration**.
2. Use **Build & Flash LittleFS** to package web assets and flash the FS partition.
3. Use **NVS Editor** to import/edit values, then write NVS to device.

## Key features

- **Cross-platform GUI**: defaults for Linux/Windows and configurable tool paths.
- **LittleFS packaging pipeline**: copies allowed web assets, gzip-compresses web files where applicable, builds `littlefs.bin`, flashes partition.
- **Integrated NVS editor**: add/update/remove rows, CSV import/export, direct flash to NVS partition.
- **Header consistency checks**: compares NVS editor keys against `include/nvs_keys.h` before write.
- **Partition-aware operations**: reads offsets/sizes from partition CSV and supports erase-region operations.
- **Saved configuration**: persists GUI settings to `uploaderGUI.json`.
