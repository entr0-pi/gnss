#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${ROOT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
DATA_DIR="${DATA_DIR:-${ROOT_DIR}/data}"
WEB_DIR_DEFAULT="${ROOT_DIR}/data/web"
WEB_DIR_ALT="${ROOT_DIR}/data/bew"
WEB_DIR="${WEB_DIR:-$WEB_DIR_DEFAULT}"
PARTITIONS_CSV="${PARTITIONS_CSV:-${ROOT_DIR}/partitions.csv}"
OUT_BIN="${OUT_BIN:-${ROOT_DIR}/build/bin/data.bin}"
MKLITTLEFS="${MKLITTLEFS:-}"

if [[ -z "${MKLITTLEFS}" ]]; then
  if command -v mklittlefs >/dev/null 2>&1; then
    MKLITTLEFS="$(command -v mklittlefs)"
  elif [[ -x "${HOME}/.platformio/packages/tool-mklittlefs/mklittlefs" ]]; then
    MKLITTLEFS="${HOME}/.platformio/packages/tool-mklittlefs/mklittlefs"
  else
    echo "[ERROR] mklittlefs not found. Set MKLITTLEFS=/path/to/mklittlefs" >&2
    exit 1
  fi
fi

if [[ ! -d "${WEB_DIR}" && -d "${WEB_DIR_ALT}" ]]; then
  WEB_DIR="${WEB_DIR_ALT}"
  echo "[WARN] WEB_DIR not found, using ${WEB_DIR_ALT}"
fi

if [[ ! -d "${DATA_DIR}" ]]; then
  echo "[ERROR] DATA_DIR not found: ${DATA_DIR}" >&2
  exit 1
fi
if [[ ! -f "${PARTITIONS_CSV}" ]]; then
  echo "[ERROR] PARTITIONS_CSV not found: ${PARTITIONS_CSV}" >&2
  exit 1
fi

FS_SIZE="$(
  awk -F, '
    /^[[:space:]]*#/ { next }
    NF < 5 { next }
    {
      subtype = $3
      gsub(/[[:space:]]/, "", subtype)
      if (tolower(subtype) == "spiffs") {
        size = $5
        gsub(/[[:space:]]/, "", size)
        print size
        exit
      }
    }
  ' "${PARTITIONS_CSV}"
)"

if [[ -z "${FS_SIZE}" ]]; then
  echo "[ERROR] Could not resolve SPIFFS size from ${PARTITIONS_CSV}" >&2
  exit 1
fi

STAGING="$(mktemp -d)"
trap 'rm -rf "${STAGING}"' EXIT

mkdir -p "$(dirname "${OUT_BIN}")"
mkdir -p "${STAGING}/web"

echo "[INFO] Staging root files from ${DATA_DIR}"
find "${DATA_DIR}" -mindepth 1 -maxdepth 1 -type f -print0 | while IFS= read -r -d '' f; do
  cp "$f" "${STAGING}/"
done

if [[ -d "${WEB_DIR}" ]]; then
  echo "[INFO] Staging web files from ${WEB_DIR}"
  find "${WEB_DIR}" -mindepth 1 -maxdepth 1 -type f -print0 | while IFS= read -r -d '' f; do
    base="$(basename "$f")"
    ext="${base##*.}"
    ext_lc="$(printf '%s' "${ext}" | tr '[:upper:]' '[:lower:]')"
    case "${ext_lc}" in
      ico|css|html|js)
        cp "$f" "${STAGING}/web/${base}"
        case "${ext_lc}" in
          css|html|js)
            gzip -c "$f" > "${STAGING}/web/${base}.gz"
            ;;
        esac
        ;;
      *)
        ;;
    esac
  done
else
  echo "[WARN] WEB_DIR not found (${WEB_DIR}). Continuing without web assets."
fi

echo "[INFO] Building LittleFS image: ${OUT_BIN}"
"${MKLITTLEFS}" -c "${STAGING}" -b 4096 -p 256 -s "${FS_SIZE}" "${OUT_BIN}"

echo "[SUCCESS] Created ${OUT_BIN}"
