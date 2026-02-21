#!/usr/bin/env bash
# Flash ESP32 binaries directly to device via serial port
# Writes 4 binaries at their correct offsets with device reset
#
# Arguments:
#   $1 (optional)  Serial port (default: /dev/ttyUSB0)
#
# Environment Variables:
#   CHIP           ESP32 variant (default: esp32c3)
#   PORT           Serial port (default: $1 or /dev/ttyUSB0)
#   BAUD           Baud rate (default: 460800)
#   SPIFFS_OFFSET  Data partition offset (substituted by docker-export-bins.sh)
#
# Usage:
#   ./flash.sh                                # Default port /dev/ttyUSB0
#   ./flash.sh /dev/ttyUSB1                  # Custom port as argument
#   PORT=/dev/ttyUSB1 ./flash.sh             # Custom port via environment

set -euo pipefail

PORT="${1:-${PORT:-/dev/ttyUSB0}}"
CHIP="${CHIP:-esp32c3}"
BAUD="${BAUD:-460800}"
SPIFFS_OFFSET="${SPIFFS_OFFSET:-__SPIFFS_OFFSET__}"

# Validate required binaries exist
for f in bootloader.bin partitions.bin firmware.bin data.bin; do
  if [[ ! -f "$f" ]]; then
    echo "[ERROR] Missing binary: $f (in $(pwd))" >&2
    exit 1
  fi
done

# Validate serial port exists
if [[ ! -e "${PORT}" ]]; then
  echo "[ERROR] Serial port not found: ${PORT}" >&2
  echo "[ERROR] Available ports: $(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | tr '\n' ' ' || echo 'none')" >&2
  exit 1
fi

echo "[INFO] Flashing to ${PORT} (${CHIP} @ ${BAUD} baud)"
python -m esptool --chip "${CHIP}" --port "${PORT}" --baud "${BAUD}" --before default-reset --after hard-reset write-flash -z \
  0x0 bootloader.bin \
  0x8000 partitions.bin \
  0x10000 firmware.bin \
  "${SPIFFS_OFFSET}" data.bin

echo "[SUCCESS] Flash complete. Device should reboot..."
