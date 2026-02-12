#!/usr/bin/env bash
set -euo pipefail
PORT="${1:-/dev/ttyUSB0}"
CHIP="${CHIP:-esp32c3}"
BAUD="${BAUD:-460800}"

esptool.py --chip "${CHIP}" --port "${PORT}" --baud "${BAUD}" --before default-reset --after hard-reset write-flash -z \
  0x0 bootloader.bin \
  0x8000 partitions.bin \
  0x10000 firmware.bin
