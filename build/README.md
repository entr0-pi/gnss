# GNSS Docker Build Export

This folder contains a two-image Docker workflow to build firmware and export flashable binaries.

## Dockerfiles

- `build/Dockerfile.base`
  - Starts from `python:3.12-slim`
  - Installs `platformio` and `esptool`
  - Pre-installs heavy global PlatformIO toolchains/packages (cached in the `gnss-base` image)

- `build/Dockerfile`
  - Starts from `gnss-base`
  - Copies `platformio.ini` and installs project dependencies (`pio pkg install`, plus `full` env when present)
  - Copies source/data/scripts and sets:
    - `ENTRYPOINT ["/usr/local/bin/docker-export-bins.sh"]`

## Build Images

Run from repository root.

### 1. Build base image (infrequent; heavy layer)

```bash
docker build -t gnss-base -f build/Dockerfile.base .
```

### 2. Build project image

```bash
docker build -t gnss -f build/Dockerfile .
```

## Run Export

Build firmware and export binaries to `build/bin/` on host:

```bash
docker run --rm -v "${PWD}/build/bin:/project/build" gnss
```

Generated files:
- `bootloader.bin`
- `partitions.bin`
- `firmware.bin`
- `data.bin`
- `gnss_esp32c3.bin` (merged image)

## Flash Merged Binary

```bash
python -m esptool --chip esp32c3 --port <PORT> write-flash 0x0 build/bin/gnss_esp32c3.bin
```

Replace `<PORT>` with your serial port.

Offsets inside merged binary:
- `0x0` bootloader
- `0x8000` partitions
- `0x10000` firmware
- `0x310000` data (LittleFS)
