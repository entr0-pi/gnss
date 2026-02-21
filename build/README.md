# GNSS Docker Build Export

This folder contains the Docker image and script used to build firmware and export flashable binaries.

## Build Image

From the repository root:

```
docker build -t gnss -f build/Dockerfile .
```

## Run Build and Export Binaries to Host

Build the firmware and export all binaries + utility scripts to `build/bin/`:

```bash
docker run --rm -v "${PWD}/build/bin:/project/build" gnss
```

**Generated files:**
- **Binaries:** `bootloader.bin`, `partitions.bin`, `firmware.bin`, `data.bin`
- **Flash helpers:** `flash.sh`, `flash.ps1`, `flash.cmd` (for direct flashing)
- **Merge helpers:** `merge.sh`, `merge.ps1`, `merge.cmd` (to combine into single binary)

## Merge Binaries into Single File

After the Docker export, merge.* scripts are available in `build/bin/`. To combine all 4 binaries into a single `gnss.bin` file:

**Bash (Linux/macOS):**
```bash
cd build/bin
./merge.sh                                 # Default: esp32c3 → gnss_esp32c3.bin
CHIP=esp32s3 ./merge.sh                   # Custom chip
OUTPUT=custom.bin ./merge.sh              # Custom output path
```

**PowerShell (Windows):**
```powershell
cd build\bin
.\merge.ps1                                # Default: esp32c3 → gnss_esp32c3.bin
.\merge.ps1 -Chip esp32s3                  # Custom chip
.\merge.ps1 -Output custom.bin             # Custom output path
```

**Batch CMD (Windows):**
```cmd
cd build\bin
merge.cmd                                  REM Default: esp32c3 → gnss_esp32c3.bin
set CHIP=esp32s3 && merge.cmd             REM Custom chip
set OUTPUT=custom.bin && merge.cmd        REM Custom output path
```

The merged binary combines all 4 files at their correct offsets:
- `0x0` → bootloader
- `0x8000` → partitions
- `0x10000` → firmware
- `0x310000` → data (LittleFS)

**Flash the merged binary:**
```bash
cd build/bin
python -m esptool --chip esp32c3 --port /dev/ttyUSB0 write-flash 0x0 gnss_esp32c3.bin
```

---

## Directory Structure

```
build/
├── Dockerfile                    (Docker image definition)
├── README.md                     (this file)
├── bin/                          (output directory, created by docker-export-bins.sh)
│   ├── *.bin                    (firmware binaries)
│   ├── flash.sh, .ps1, .cmd     (flash scripts from bin_utils/)
│   └── merge.sh, .ps1, .cmd     (merge scripts from bin_utils/)
└── scripts/
    ├── docker-export-bins.sh    (orchestrator: builds & exports)
    └── bin_utils/               (utility scripts)
        ├── create-data-bin.sh   (builds data.bin from data/)
        ├── flash.sh             (flashes binaries to device)
        ├── flash.ps1            (PowerShell variant)
        ├── flash.cmd            (Windows CMD variant)
        ├── merge.sh             (merges bins into single file)
        ├── merge.ps1            (PowerShell variant)
        └── merge.cmd            (Windows CMD variant)
```