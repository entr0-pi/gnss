#!/usr/bin/env bash
set -euo pipefail

ENV_NAME="${PIO_ENV:-full}"
OUT_DIR="${OUT_DIR:-build}"
BUILD_DIR=".pio/build/${ENV_NAME}"
PARTITIONS_CSV="${PARTITIONS_CSV:-partitions.csv}"
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

for script in flash.sh flash.ps1 flash.cmd; do
  src="${BIN_UTILS_DIR}/${script}"
  dst="${OUT_DIR}/${script}"
  if [[ ! -f "${src}" ]]; then
    echo "[ERROR] Missing script template: ${src}" >&2
    exit 1
  fi
  cp "${src}" "${dst}"
  sed -i "s/__SPIFFS_OFFSET__/${SPIFFS_OFFSET}/g" "${dst}"
  if [[ "${script}" == "flash.sh" ]]; then
    chmod +x "${dst}"
  fi
done

for script in merge.sh merge.ps1 merge.cmd; do
  src="${BIN_UTILS_DIR}/${script}"
  dst="${OUT_DIR}/${script}"
  if [[ ! -f "${src}" ]]; then
    echo "[ERROR] Missing merge script: ${src}" >&2
    exit 1
  fi
  cp "${src}" "${dst}"
  if [[ "${script}" == "merge.sh" ]]; then
    chmod +x "${dst}"
  fi
done

echo "[INFO] Exported binaries to ${OUT_DIR}:"
ls -1 "${OUT_DIR}"/*.bin

echo "[INFO] Scripts generated:"
echo "[INFO]   Flash helpers: flash.sh, flash.ps1, flash.cmd"
echo "[INFO]   Merge helpers: merge.sh, merge.ps1, merge.cmd"
echo "[INFO] SPIFFS offset for data.bin: ${SPIFFS_OFFSET}"
echo "[INFO] To flash binaries directly:"
echo "[INFO]   Linux/macOS: cd ${OUT_DIR} && ./flash.sh /dev/ttyUSB0"
echo "[INFO]   Windows PowerShell: cd ${OUT_DIR}; ./flash.ps1 -Port COM8"
echo "[INFO]   Windows CMD: cd ${OUT_DIR} && flash.cmd COM8"
echo "[INFO] To merge binaries into gnss.bin, use merge.* scripts from ${OUT_DIR}"
