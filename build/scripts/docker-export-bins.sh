#!/usr/bin/env bash
set -euo pipefail

ENV_NAME="${PIO_ENV:-full}"
OUT_DIR="${OUT_DIR:-build}"
BUILD_DIR=".pio/build/${ENV_NAME}"
PARTITIONS_CSV="${PARTITIONS_CSV:-partitions.csv}"
CHIP="${CHIP:-esp32c3}"
BIN_UTILS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/bin_utils" && pwd)"

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
"${BIN_UTILS_DIR}/create-data-bin.sh"

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

if [[ ! -d "${BIN_UTILS_DIR}" ]]; then
  echo "[ERROR] Binary utilities directory not found: ${BIN_UTILS_DIR}" >&2
  exit 1
fi

echo "[INFO] Merging binaries into gnss_${CHIP}.bin"
python -m esptool --chip "${CHIP}" merge-bin \
  -o "${OUT_DIR}/gnss_${CHIP}.bin" \
  0x0 "${OUT_DIR}/bootloader.bin" \
  0x8000 "${OUT_DIR}/partitions.bin" \
  0x10000 "${OUT_DIR}/firmware.bin" \
  "${SPIFFS_OFFSET}" "${OUT_DIR}/data.bin"

echo "[INFO] Exported binaries to ${OUT_DIR}:"
ls -1 "${OUT_DIR}"/*.bin

echo "[SUCCESS] Build complete. Merged binary ready to flash:"
ls -1 "${OUT_DIR}"/gnss_*.bin
