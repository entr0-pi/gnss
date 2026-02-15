#!/usr/bin/env bash
set -euo pipefail
PORT="${1:-/dev/ttyUSB0}"
CHIP="${CHIP:-esp32c3}"
BAUD="${BAUD:-460800}"
SPIFFS_OFFSET="${SPIFFS_OFFSET:-0x310000}"

for f in bootloader.bin partitions.bin firmware.bin data.bin; do
  [[ -f "$f" ]] || { echo "[ERROR] Missing: $f"; exit 1; }
done
if [[ ! -e "${PORT}" ]]; then
  echo "[ERROR] Serial port not found: ${PORT}"; exit 1
fi

python -m esptool --chip "${CHIP}" --port "${PORT}" --baud "${BAUD}" --before default-reset --after hard-reset write-flash -z \
  0x0 bootloader.bin \
  0x8000 partitions.bin \
  0x10000 firmware.bin \
  "${SPIFFS_OFFSET}" data.bin
