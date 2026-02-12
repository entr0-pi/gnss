# GNSS Docker Build Export

This folder contains the Docker image and script used to build firmware and export flashable binaries.

## Build Image

From the repository root:

```powershell
docker build -t gnss-build -f build/Dockerfile .
```

## Run Build

Default environment is `full` (from `platformio.ini`):

```powershell
docker run --rm gnss-build
```

## Export Binaries to Host

Write generated files into the host `build/` folder:

```powershell
docker run --rm -v "${PWD}/build/bin:/project/build" gnss-build
```

Generated files include:
- `bootloader.bin`
- `partitions.bin`
- `firmware.bin`
- `flash.sh`
- `flash.ps1`
- `flash.cmd`

## Optional Overrides

Set a specific PlatformIO environment:

```powershell
docker run --rm -e PIO_ENV=full gnss-build
```

Set a custom output directory inside the container:

```powershell
docker run --rm -e OUT_DIR=build gnss-build
```
