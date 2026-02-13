#!/usr/bin/env bash
set -euo pipefail

ENV_NAME="${PIO_ENV:-full}"
OUT_DIR="${OUT_DIR:-build}"
BUILD_DIR=".pio/build/${ENV_NAME}"
PARTITIONS_CSV="${PARTITIONS_CSV:-partitions.csv}"

mkdir -p "${OUT_DIR}"

if [[ ! -f "${PARTITIONS_CSV}" ]]; then
  echo "[ERROR] PARTITIONS_CSV not found: ${PARTITIONS_CSV}" >&2
  exit 1
fi

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

echo "[INFO] Building data.bin from data/ + data/web inside container"
ROOT_DIR="/project" \
OUT_BIN="${OUT_DIR}/data.bin" \
MKLITTLEFS="${MKLITTLEFS:-/root/.platformio/packages/tool-mklittlefs/mklittlefs}" \
/usr/local/bin/create-data-bin.sh

SPIFFS_OFFSET="$(
  awk -F, '
    /^[[:space:]]*#/ { next }
    NF < 4 { next }
    {
      subtype = $3
      gsub(/[[:space:]]/, "", subtype)
      if (tolower(subtype) == "spiffs") {
        offset = $4
        gsub(/[[:space:]]/, "", offset)
        print offset
        exit
      }
    }
  ' "${PARTITIONS_CSV}"
)"

if [[ -z "${SPIFFS_OFFSET}" ]]; then
  echo "[ERROR] Could not resolve SPIFFS offset from ${PARTITIONS_CSV}" >&2
  exit 1
fi

cat > "${OUT_DIR}/flash.sh" <<'SCRIPT'
#!/usr/bin/env bash
set -euo pipefail
PORT="${1:-/dev/ttyUSB0}"
CHIP="${CHIP:-esp32c3}"
BAUD="${BAUD:-460800}"
SPIFFS_OFFSET="${SPIFFS_OFFSET:-__SPIFFS_OFFSET__}"

python -m esptool --chip "${CHIP}" --port "${PORT}" --baud "${BAUD}" --before default-reset --after hard-reset write-flash -z \
  0x0 bootloader.bin \
  0x8000 partitions.bin \
  0x10000 firmware.bin \
  "${SPIFFS_OFFSET}" data.bin
SCRIPT
sed -i "s/__SPIFFS_OFFSET__/${SPIFFS_OFFSET}/g" "${OUT_DIR}/flash.sh"
chmod +x "${OUT_DIR}/flash.sh"

cat > "${OUT_DIR}/flash.ps1" <<'SCRIPT'
param(
    [string]$Port = "COM8",
    [string]$Chip = "esp32c3",
    [int]$Baud = 460800,
    [string]$SpiffsOffset = "__SPIFFS_OFFSET__"
)

python -m esptool --chip $Chip --port $Port --baud $Baud --before default-reset --after hard-reset write-flash -z `
  0x0 bootloader.bin `
  0x8000 partitions.bin `
  0x10000 firmware.bin `
  $SpiffsOffset data.bin
SCRIPT
sed -i "s/__SPIFFS_OFFSET__/${SPIFFS_OFFSET}/g" "${OUT_DIR}/flash.ps1"

cat > "${OUT_DIR}/flash.cmd" <<'SCRIPT'
@echo off
set PORT=%1
if "%PORT%"=="" set PORT=COM8
set CHIP=%CHIP%
if "%CHIP%"=="" set CHIP=esp32c3
set BAUD=%BAUD%
if "%BAUD%"=="" set BAUD=460800
set SPIFFS_OFFSET=%SPIFFS_OFFSET%
if "%SPIFFS_OFFSET%"=="" set SPIFFS_OFFSET=__SPIFFS_OFFSET__

python -m esptool --chip %CHIP% --port %PORT% --baud %BAUD% --before default-reset --after hard-reset write-flash -z ^
  0x0 bootloader.bin ^
  0x8000 partitions.bin ^
  0x10000 firmware.bin ^
  %SPIFFS_OFFSET% data.bin
SCRIPT
sed -i "s/__SPIFFS_OFFSET__/${SPIFFS_OFFSET}/g" "${OUT_DIR}/flash.cmd"

echo "[INFO] Exported binaries to ${OUT_DIR}:"
ls -1 "${OUT_DIR}"/*.bin

echo "[INFO] Flash helpers generated: flash.sh, flash.ps1, flash.cmd"
echo "[INFO] SPIFFS offset for data.bin: ${SPIFFS_OFFSET}"
echo "[INFO] Linux/macOS: cd ${OUT_DIR} && ./flash.sh /dev/ttyUSB0"
echo "[INFO] Windows PowerShell: cd ${OUT_DIR}; ./flash.ps1 -Port COM8"
echo "[INFO] Windows CMD: cd ${OUT_DIR} && flash.cmd COM8"
