#!/usr/bin/env python3
"""Sync docs/boards.json with the PlatformIO espressif32 board list and enrich it.

Source of truth = https://github.com/platformio/platformio-docs/tree/develop/boards/espressif32
  * board on the list, not in the JSON  -> ADDED   (fields built from its PlatformIO manifest)
  * board in the JSON, not on the list  -> REMOVED
  * board on the list and in the JSON   -> KEPT    (binary fields preserved, specs refreshed)

Specs (MCU / FREQUENCY / FLASH / RAM / VENDOR) come from the official manifests
platform-espressif32/boards/<id>.json, which the docs pages are generated from.

Usage:
  python utils/boards-update/enrich_boards.py
  # Defaults to reading and updating docs/boards.json, regardless of working directory.
  python utils/boards-update/enrich_boards.py in.json out.json
Offline / testing:
  python utils/boards-update/enrich_boards.py in.json out.json --docs DIR_WITH_<id>.rst --manifests DIR_WITH_<id>.json
Optional: set GITHUB_TOKEN to avoid API rate limits.
"""
import json, os, sys, urllib.request, urllib.error
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BOARDS = ROOT / "docs" / "boards.json"

DOCS_API = "https://api.github.com/repos/platformio/platformio-docs/contents/boards/espressif32?ref=develop"
MANIFEST = "https://raw.githubusercontent.com/platformio/platform-espressif32/develop/boards/{}.json"

# Chips for which firmware binaries exist
BINARY_CHIPS = {"esp32", "esp32c3", "esp32s3"}

# Port-type knowledge carried over from the first pass (C3 / S3 boards with a USB-to-UART bridge)
UART_BOARDS = {
    "esp32-c3-devkitc-02": "high", "esp32-c3-devkitm-1": "medium", "esp32-c3-m1i-kit": "medium",
    "rymcu-esp32-c3-devkitm-1": "medium", "ttgo-t-oi-plus": "low", "wifiduino32c3": "low",
    "4d_systems_esp32s3_gen4_r8n16": "low", "edgebox-esp-100": "low", "esp32-s3-devkitc-1": "medium",
    "esp32-s3-devkitm-1": "medium", "esp32s3camlcd": "low", "freenove_esp32_s3_wroom": "medium",
    "heltec_wifi_kit_32_V3": "high", "heltec_wifi_lora_32_V3": "high", "lolin_s3": "low",
    "rymcu-esp32-s3-devkitc-1": "medium",
}

def http_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": "enrich-boards"})
    if os.environ.get("GITHUB_TOKEN") and "api.github.com" in url:
        req.add_header("Authorization", "Bearer " + os.environ["GITHUB_TOKEN"])
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)

def list_board_ids(docs_dir):
    """Board IDs = file names of boards/espressif32/<id>.rst"""
    if docs_dir:
        names = os.listdir(docs_dir)
    else:
        names = [e["name"] for e in http_json(DOCS_API) if e["type"] == "file"]
    return sorted(n[:-4] for n in names if n.endswith(".rst"))

def load_manifest(board_id, local):
    try:
        if local:
            p = os.path.join(local, board_id + ".json")
            return json.load(open(p, encoding="utf-8")) if os.path.exists(p) else None
        return http_json(MANIFEST.format(board_id))
    except urllib.error.HTTPError:
        return None

def human_bytes(n):
    if n >= 1024 * 1024 and n % (1024 * 1024) == 0:
        return f"{n // (1024 * 1024)}MB"
    return f"{round(n / 1024)}KB"

def extract_specs(m):
    if not m:
        return {"MCU": None, "FREQUENCY": None, "FLASH": None, "RAM": None, "VENDOR": None}
    b, u = m.get("build", {}), m.get("upload", {})
    f_cpu = str(b.get("f_cpu", "")).rstrip("Ll")
    return {
        "MCU": b.get("mcu", "").upper().replace("-", "") or None,
        "FREQUENCY": f"{int(f_cpu) // 1_000_000}MHz" if f_cpu.isdigit() else None,
        "FLASH": u.get("flash_size"),
        "RAM": human_bytes(u["maximum_ram_size"]) if "maximum_ram_size" in u else None,
        "VENDOR": m.get("vendor"),
    }

def binary_fields(board_id, chip):
    """Binary info for a NEW board (existing boards keep what they already have)."""
    if chip not in BINARY_CHIPS:
        return {"BINARY_AVAILABLE": False, "BINARY_AVAILABLE_SUFFIXES": [],
                "BINARY_MOST_LIKELY": None, "BINARY_MOST_LIKELY_PORT": None, "BINARY_CONFIDENCE": None}
    if chip == "esp32":
        return {"BINARY_AVAILABLE": True, "BINARY_AVAILABLE_SUFFIXES": ["_esp32.bin"],
                "BINARY_MOST_LIKELY": "_esp32.bin", "BINARY_MOST_LIKELY_PORT": "n/a", "BINARY_CONFIDENCE": "high"}
    if board_id in UART_BOARDS:
        port, conf = "uart", UART_BOARDS[board_id]
    else:
        port, conf = "usb", "low"          # unknown new C3/S3 board -> best guess
    return {"BINARY_AVAILABLE": True,
            "BINARY_AVAILABLE_SUFFIXES": [f"_{chip}_uart.bin", f"_{chip}_usb.bin"],
            "BINARY_MOST_LIKELY": f"_{chip}_{port}.bin", "BINARY_MOST_LIKELY_PORT": port, "BINARY_CONFIDENCE": conf}

def main():
    opt = lambda k: sys.argv[sys.argv.index(k) + 1] if k in sys.argv else None
    docs_dir, man_dir = opt("--docs"), opt("--manifests")
    args = [a for a in sys.argv[1:] if not a.startswith("--") and a not in (docs_dir, man_dir)]
    src = args[0] if args else DEFAULT_BOARDS
    dst = args[1] if len(args) > 1 else DEFAULT_BOARDS

    data = json.load(open(src, encoding="utf-8"))
    existing = {b["TARGET_BOARD"]: b for b in data["boards"]}
    default_gnss = Counter(b.get("TARGET_GNSS") for b in data["boards"]).most_common(1)[0][0]

    ids = list_board_ids(docs_dir)
    if not ids:
        sys.exit("Board list is empty - refusing to overwrite anything.")
    with ThreadPoolExecutor(8) as ex:
        manifests = dict(zip(ids, ex.map(lambda i: load_manifest(i, man_dir), ids)))

    out, added, no_manifest = [], [], []
    for bid in [i for i in existing if i in ids]:                  # kept, original order
        b = dict(existing[bid]); b.update(extract_specs(manifests[bid])); out.append(b)
    for bid in ids:                                               # added
        if bid in existing: continue
        m = manifests[bid]
        if m is None: no_manifest.append(bid)
        chip = (m or {}).get("build", {}).get("mcu", "")
        b = {"TARGET_BOARD": bid, "TARGET_CHIP": chip, "TARGET_LABEL": (m or {}).get("name", bid),
             "TARGET_GNSS": default_gnss}
        b.update(binary_fields(bid, chip)); b.update(extract_specs(m))
        out.append(b); added.append(bid)
    removed = [i for i in existing if i not in ids]

    json.dump({"boards": out}, open(dst, "w", encoding="utf-8"), indent=2, ensure_ascii=False)
    print(f"{len(out)} boards written to {dst}  (list: {len(ids)})")
    print(f"  kept {len(out) - len(added)} | added {len(added)} | removed {len(removed)}")
    if added:   print("  ADDED:  ", ", ".join(added))
    if removed: print("  REMOVED:", ", ".join(removed))
    if no_manifest: print("  No manifest (specs empty):", ", ".join(no_manifest))
    unsupported = [b["TARGET_BOARD"] for b in out if not b.get("BINARY_AVAILABLE")]
    if unsupported: print(f"  {len(unsupported)} boards on chips without a binary -> BINARY_AVAILABLE=false")

if __name__ == "__main__":
    main()
