#!/usr/bin/env bash
set -euo pipefail

CHIP="${CHIP:-esp32c3}"
BIN_DIR="${BIN_DIR:-build/bin}"
OUTPUT="${OUTPUT:-${BIN_DIR}/gnss_${CHIP}.bin}"
SPIFFS_OFFSET="${SPIFFS_OFFSET:-0x310000}"

mkdir -p "${BIN_DIR}"

for f in bootloader.bin partitions.bin firmware.bin data.bin; do
  [[ -f "${BIN_DIR}/$f" ]] || { echo "[ERROR] Missing: ${BIN_DIR}/$f"; exit 1; }
done

echo "[INFO] Merging binaries into ${OUTPUT}"
python -m esptool --chip "${CHIP}" merge-bin \
  -o "${OUTPUT}" \
  0x0 "${BIN_DIR}/bootloader.bin" \
  0x8000 "${BIN_DIR}/partitions.bin" \
  0x10000 "${BIN_DIR}/firmware.bin" \
  "${SPIFFS_OFFSET}" "${BIN_DIR}/data.bin"

echo "[INFO] Successfully created: ${OUTPUT}"
echo "[INFO] To flash, run:"
echo "[INFO]   python -m esptool --chip ${CHIP} --port <PORT> write-flash 0x0 ${OUTPUT}"
echo "[INFO]   (replace <PORT> with your device port, e.g. /dev/ttyUSB0)"
