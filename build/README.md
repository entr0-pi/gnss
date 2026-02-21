# GNSS Docker Build Export

This folder contains the Docker image and script used to build firmware and export flashable binaries.

## Build Image

From the repository root:

```
docker build -t gnss -f build/Dockerfile .
```

## Run Build and Export Binaries to Host

Build the firmware and export all binaries to `build/bin/`:

```bash
docker run --rm -v "${PWD}/build/bin:/project/build" gnss
```

**Generated files:**
- **Binaries:** `bootloader.bin`, `partitions.bin`, `firmware.bin`, `data.bin`
- **Merged binary:** `gnss_esp32c3.bin` (combines all 4 at correct offsets)

## Flash the Merged Binary

The Docker export automatically creates a merged `gnss_*.bin` file. Flash it directly:

```bash
python -m esptool --chip esp32c3 --port /dev/ttyUSB0 write-flash 0x0 build/bin/gnss_esp32c3.bin
```

The merged binary contains all 4 components at their correct offsets:
- `0x0` → bootloader
- `0x8000` → partitions
- `0x10000` → firmware
- `0x310000` → data (LittleFS)

---

## Directory Structure

```
build/
├── Dockerfile                    (Docker image definition)
├── README.md                     (this file)
├── bin/                          (output directory, created by docker-export-bins.sh)
│   └── *.bin                    (bootloader, partitions, firmware, data, gnss_merged)
└── scripts/
    ├── docker-export-bins.sh    (orchestrator: builds & exports)
    └── bin_utils/               (utility scripts used internally by docker-export-bins.sh)
        ├── create-data-bin.sh   (builds data.bin from data/)
        ├── merge.sh             (merges bins into single file)
        ├── merge.ps1            (PowerShell variant - for manual merging)
        ├── merge.cmd            (Windows CMD variant - for manual merging)
        ├── flash.sh             (flashes binaries to device - for manual use)
        ├── flash.ps1            (PowerShell variant - for manual use)
        └── flash.cmd            (Windows CMD variant - for manual use)
```