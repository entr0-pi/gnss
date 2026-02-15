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

### Required packages

**Python (from repo root)**
```bash
pip install -r utils/uploader/requirements.txt
```

`requirements.txt` currently includes:
- `esptool`
- `sv_ttk`
- `esp_idf_nvs_partition_gen`

### Required external tools/files

- `mklittlefs` binary (manual install/download)
- `nvs_partition_gen.py` (from ESP-IDF)

### Typical workflow

1. Set serial port, chip family, partition CSV, and tool paths in **Configuration**.
2. Use **Build & Flash LittleFS** to package web assets and flash the FS partition.
3. Use **NVS Editor** to import/edit values, then write NVS to device.

## Key features

- **Cross-platform GUI**: defaults for Linux/Windows and configurable tool paths.
- **Tool auto-discovery**:
  - `mklittlefs`: looks under bundled path and `lib/mklittlefs/`
  - `nvs_partition_gen.py`: looks under `utils/uploader/lib/` and project `lib/`
- **LittleFS packaging pipeline**:
  - stages files from `data/` root
  - stages `data/web/` assets with allowed extensions (`.ico`, `.css`, `.html`, `.js`)
  - generates gzip variants for compressible web assets (`.css`, `.html`, `.js`)
  - builds `littlefs.bin` and flashes the `spiffs` partition
- **Integrated NVS editor**: add/update/remove rows, CSV import/export, direct flash to NVS partition.
- **Header consistency checks**: compares NVS editor keys against `include/nvs_keys.h` before write.
- **Partition-aware operations**: reads offsets/sizes from partition CSV and supports erase-region operations.
- **Saved configuration**: persists GUI settings to `uploaderGUI.json`.
- **Safety controls**:
  - optional "Erase FS partition before flash"
  - optional "Erase NVS before write"
  - explicit "Erase NVS (No Write)" action
  - warns before writing NVS when keys mismatch `nvs_keys.h`

## Notes

- The app checks Python imports for `sv_ttk` and `esptool` at startup.
- `nvs_partition_gen.py` is executed by path configured in the GUI (it is not imported as a Python module).
