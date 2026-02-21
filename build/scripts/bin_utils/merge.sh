#!/usr/bin/env bash
# Merge 4 ESP32 binaries into a single flashable image
# Combines: bootloader, partitions, firmware, and data at their correct offsets
#
# Environment Variables:
#   CHIP             ESP32 variant (default: esp32c3)
#   BIN_DIR          Directory containing .bin files (default: build/bin)
#   OUTPUT           Output gnss.bin path (default: ${BIN_DIR}/gnss_${CHIP}.bin)
#   SPIFFS_OFFSET    Data partition offset (default: 0x310000)

set -euo pipefail

CHIP="${CHIP:-esp32c3}"
BIN_DIR="${BIN_DIR:-build/bin}"
OUTPUT="${OUTPUT:-${BIN_DIR}/gnss_${CHIP}.bin}"
SPIFFS_OFFSET="${SPIFFS_OFFSET:-0x310000}"

mkdir -p "${BIN_DIR}"

# Validate required binaries exist
for f in bootloader.bin partitions.bin firmware.bin data.bin; do
  if [[ ! -f "${BIN_DIR}/${f}" ]]; then
    echo "[ERROR] Missing binary: ${BIN_DIR}/${f}" >&2
    exit 1
  fi
done

echo "[INFO] Merging binaries into ${OUTPUT}"
python -m esptool --chip "${CHIP}" merge-bin \
  -o "${OUTPUT}" \
  0x0 "${BIN_DIR}/bootloader.bin" \
  0x8000 "${BIN_DIR}/partitions.bin" \
  0x10000 "${BIN_DIR}/firmware.bin" \
  "${SPIFFS_OFFSET}" "${BIN_DIR}/data.bin"

echo "[SUCCESS] Created merged binary: ${OUTPUT}"
echo "[INFO] To flash, run:"
echo "[INFO]   python -m esptool --chip ${CHIP} --port <PORT> write-flash 0x0 ${OUTPUT}"
echo "[INFO]   (replace <PORT> with your device port, e.g. /dev/ttyUSB0)"
