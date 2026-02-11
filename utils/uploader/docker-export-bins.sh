#!/usr/bin/env bash
set -euo pipefail

ENV_NAME="${PIO_ENV:-lolin_c3_mini}"
OUT_DIR="${OUT_DIR:-build}"
BUILD_DIR=".pio/build/${ENV_NAME}"

mkdir -p "${OUT_DIR}"

echo "[INFO] Building PlatformIO environment: ${ENV_NAME}"
pio run -e "${ENV_NAME}"

for bin in bootloader.bin partitions.bin firmware.bin; do
  src="${BUILD_DIR}/${bin}"
  if [[ ! -f "${src}" ]]; then
    echo "[ERROR] Missing build artifact: ${src}" >&2
    exit 1
  fi
  cp "${src}" "${OUT_DIR}/${bin}"
done

cat > "${OUT_DIR}/flash.sh" <<'SCRIPT'
#!/usr/bin/env bash
set -euo pipefail
PORT="${1:-/dev/ttyUSB0}"
CHIP="${CHIP:-esp32c3}"
BAUD="${BAUD:-460800}"

esptool.py --chip "${CHIP}" --port "${PORT}" --baud "${BAUD}" --before default_reset --after hard_reset write_flash -z \
  0x1000 bootloader.bin \
  0x8000 partitions.bin \
  0x10000 firmware.bin
SCRIPT
chmod +x "${OUT_DIR}/flash.sh"

cat > "${OUT_DIR}/flash.ps1" <<'SCRIPT'
param(
    [string]$Port = "COM3",
    [string]$Chip = "esp32c3",
    [int]$Baud = 460800
)

python -m esptool --chip $Chip --port $Port --baud $Baud --before default_reset --after hard_reset write_flash -z `
  0x1000 bootloader.bin `
  0x8000 partitions.bin `
  0x10000 firmware.bin
SCRIPT

cat > "${OUT_DIR}/flash.cmd" <<'SCRIPT'
@echo off
set PORT=%1
if "%PORT%"=="" set PORT=COM3
set CHIP=%CHIP%
if "%CHIP%"=="" set CHIP=esp32c3
set BAUD=%BAUD%
if "%BAUD%"=="" set BAUD=460800

python -m esptool --chip %CHIP% --port %PORT% --baud %BAUD% --before default_reset --after hard_reset write_flash -z ^
  0x1000 bootloader.bin ^
  0x8000 partitions.bin ^
  0x10000 firmware.bin
SCRIPT

echo "[INFO] Exported binaries to ${OUT_DIR}:"
ls -1 "${OUT_DIR}"/*.bin

echo "[INFO] Flash helpers generated: flash.sh, flash.ps1, flash.cmd"
echo "[INFO] Linux/macOS: cd ${OUT_DIR} && ./flash.sh /dev/ttyUSB0"
echo "[INFO] Windows PowerShell: cd ${OUT_DIR}; ./flash.ps1 -Port COM3"
echo "[INFO] Windows CMD: cd ${OUT_DIR} && flash.cmd COM3"
