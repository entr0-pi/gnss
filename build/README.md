# GNSS Docker Build Export

This folder contains the Docker image and script used to build firmware and export flashable binaries.

## Build Image

From the repository root:

```
docker build --no-cache -t gnss -f build/Dockerfile .
```

## Run Build and Export Binaries to Host

Write generated files into the host `build/bin/` folder:

```
docker run --rm -v "${PWD}/build/bin:/project/build" gnss
```

Generated files include:
- `bootloader.bin`
- `partitions.bin`
- `firmware.bin`
- `data.bin`
- `flash.sh`
- `flash.ps1`
- `flash.cmd`