#!/usr/bin/env python3
"""Board retargeting script for the GNSS ESP32 project.

Reads .env from project root and updates all board/chip-specific
references across the codebase.  Safe to run multiple times (idempotent).

Usage:
    python utils/board/retarget.py              # Apply changes
    python utils/board/retarget.py --dry-run    # Preview only
"""

import argparse
import re
import sys
from pathlib import Path

# ── Known chip families ──────────────────────────────────────────────

BOOTLOADER_OFFSETS = {
    # 0x1000
    "esp32":    "0x1000",
    "esp32s2":  "0x1000",
    # 0x0
    "esp32s3":  "0x0",
    "esp32c2":  "0x0",
    "esp32c3":  "0x0",
    "esp32c6":  "0x0",
    "esp32c61": "0x0",
    "esp32h2":  "0x0",
    # 0x2000
    "esp32c5":  "0x2000",
    "esp32p4":  "0x2000",
    "esp32h4":  "0x2000",
    "esp32s31": "0x2000",
}

KNOWN_CHIPS = set(BOOTLOADER_OFFSETS)

# Chips buildable with build/Dockerfile.base
# (espressif32 @ 6.13.0 + Arduino core 2.0.17). Others need Arduino core 3.x.
SUPPORTED_CHIPS = {"esp32", "esp32s2", "esp32s3", "esp32c3"}
DOCKER_STACK = "build/Dockerfile.base (espressif32 6.13.0 / Arduino core 2.0.17)"


def check_chip(chip: str):
    """Return an error message if `chip` cannot be built, else None."""
    supported = ", ".join(sorted(SUPPORTED_CHIPS))
    if chip not in KNOWN_CHIPS:
        return f"TARGET_CHIP='{chip}' is not recognised. Supported: {supported}"
    if chip not in SUPPORTED_CHIPS:
        return (f"TARGET_CHIP='{chip}' (bootloader {BOOTLOADER_OFFSETS[chip]}) is not "
                f"supported by {DOCKER_STACK}; it needs Arduino core 3.x.\n"
                f"  Supported: {supported}")
    return None

# ── Helpers ──────────────────────────────────────────────────────────

def chip_short_name(chip: str) -> str:
    """esp32c3 -> ESP32-C3, esp32s3 -> ESP32-S3, esp32 -> ESP32."""
    up = chip.upper()  # ESP32C3
    if len(up) > 5:
        return up[:5] + "-" + up[5:]  # ESP32-C3
    return up  # ESP32


def load_env(path: Path) -> dict:
    """Parse a simple KEY=VALUE .env file (no shell expansion)."""
    env = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            key, _, value = line.partition("=")
            env[key.strip()] = value.strip()
    return env


def validate(env: dict) -> None:
    required = ["TARGET_BOARD", "TARGET_CHIP", "TARGET_LABEL", "TARGET_GNSS"]
    for key in required:
        if not env.get(key):
            sys.exit(f"[ERROR] {key} is missing or empty in .env")
    err = check_chip(env["TARGET_CHIP"])
    if err:
        sys.exit(f"[ERROR] {err}")

# ── Replacement engine ───────────────────────────────────────────────

class FileReplacer:
    """Apply regex replacements and track what changed."""

    def __init__(self, dry_run: bool):
        self.dry_run = dry_run
        self.changes: list[dict] = []
        self.files_modified: set[str] = set()

    def process(self, filepath: Path, rules: list[tuple[re.Pattern, str]]) -> None:
        # newline="" preserves original line endings (LF for .sh, CRLF for .cmd)
        with open(filepath, encoding="utf-8", newline="") as f:
            text = f.read()
        lines = text.splitlines(keepends=True)
        modified = False

        for i, line in enumerate(lines):
            for pattern, repl in rules:
                new_line = pattern.sub(repl, line)
                if new_line != line:
                    self.changes.append({
                        "file": str(filepath),
                        "line_no": i + 1,
                        "before": line.rstrip(),
                        "after": new_line.rstrip(),
                    })
                    lines[i] = new_line
                    line = new_line
                    modified = True

        if modified:
            rel = filepath.name
            self.files_modified.add(rel)
            if not self.dry_run:
                with open(filepath, "w", encoding="utf-8", newline="") as f:
                    f.write("".join(lines))

# ── Rule definitions ─────────────────────────────────────────────────

def build_rules(root: Path, env: dict) -> list[tuple[Path, list]]:
    B = env["TARGET_BOARD"]
    C = env["TARGET_CHIP"]
    L = env["TARGET_LABEL"]
    G = env["TARGET_GNSS"]
    O = BOOTLOADER_OFFSETS[C]
    CSN = chip_short_name(C)

    # Helper: re.escape for literal insertion into replacement strings
    # (not needed for regex patterns, but safe for replacement text)
    Le = L.replace("\\", "\\\\")
    Ge = G.replace("\\", "\\\\")

    return [
        # ── 1. platformio.ini ────────────────────────────────────────
        (root / "platformio.ini", [
            (re.compile(r"^(board\s*=\s*).*", re.MULTILINE),
             rf"\g<1>{B}"),
        ]),

        # ── 2. build-tester platformio.ini.test ─────────────────────
        (root / "utils" / "build-tester" / "platformio.ini.test", [
            (re.compile(r"^\[env:\w[\w\-]*\]", re.MULTILINE),
             f"[env:{B}]"),
            (re.compile(r"^(board\s*=\s*).*", re.MULTILINE),
             rf"\g<1>{B}"),
        ]),

        # ── 3. docker-export-bins.sh + bin_utils flash scripts ──────
        # Bootloader offset line, e.g.:
        #   0x0 "${OUT_DIR}/bootloader.bin" \     (merge-bin)
        #   0x0 bootloader.bin ^                  (flash helpers)
        *[
            (path, [
                # Bash: CHIP="${CHIP:-esp32c3}"
                (re.compile(r'(CHIP="\$\{CHIP:-)\w+(\}")'),
                 rf"\g<1>{C}\g<2>"),
                # PowerShell: [string]$Chip = "esp32c3"
                (re.compile(r'(\[string\]\$Chip\s*=\s*")\w+(")'),
                 rf"\g<1>{C}\g<2>"),
                # CMD: if "!CHIP!"=="" set "CHIP=esp32c3"
                (re.compile(r'(set "?CHIP=)\w+'),
                 rf"\g<1>{C}"),
                # Help text: "ESP32 variant (default: esp32c3)"
                (re.compile(r"(ESP32 variant \(default:\s*)\w+(\))"),
                 rf"\g<1>{C}\g<2>"),
                (re.compile(r"^(\s*)0x[0-9a-fA-F]+(\s+\S*bootloader\.bin)"),
                 rf"\g<1>{O}\g<2>"),
            ])
            for path in (
                root / "build" / "scripts" / "docker-export-bins.sh",
                root / "build" / "scripts" / "bin_utils" / "flash.sh",
                root / "build" / "scripts" / "bin_utils" / "flash.ps1",
                root / "build" / "scripts" / "bin_utils" / "flash.cmd",
            )
        ],

        # ── 4. Web UI index.html ────────────────────────────────────
        (root / "data" / "web" / "index.html", [
            (re.compile(r"(<title>).*?(</title>)"),
             rf"\g<1>{CSN} Status\g<2>"),
            (re.compile(r'(class="sub">).*?(</div>)'),
             rf"\g<1>Web Server on {Le} with {Ge}\g<2>"),
        ]),

        # ── 5. Web UI app.js ────────────────────────────────────────
        (root / "data" / "web" / "app.js", [
            (re.compile(r'(confirm\("Restart ).*?( now\?"\))'),
             rf"\g<1>{CSN}\g<2>"),
        ]),

        # ── 6. INSTALL.md (bootloader offset) ───────────────────────
        (root / "INSTALL.md", [
            (re.compile(r"(`bootloader\.bin`\s*->\s*`)0x[0-9a-fA-F]+"),
             rf"\g<1>{O}"),
        ]),

        # ── 7. build-tester README.md ────────────────────────────────────
        (root / "utils" / "build-tester" / "README.md", [
            # Title: "# ESP32-C3 Build Flag Tester"
            (re.compile(r"^(#\s*)\S+(\s+Build Flag Tester)", re.MULTILINE),
             rf"\g<1>{CSN}\g<2>"),
            # Body: "for the ESP32-C3, validating..."
            (re.compile(r"(for the\s+)[\w\-]+(,?\s+validating)"),
             rf"\g<1>{CSN}\g<2>"),
        ]),
    ]

# ── Report ───────────────────────────────────────────────────────────

def print_report(replacer: FileReplacer, dry_run: bool) -> None:
    tag = "[DRY RUN] " if dry_run else ""

    if not replacer.changes:
        print(f"{tag}No changes needed. All files already match .env.")
        return

    current_file = None
    for c in replacer.changes:
        if c["file"] != current_file:
            current_file = c["file"]
            print(f"\n{tag}{current_file}")
        print(f"  L{c['line_no']:>4d}  - {c['before']}")
        print(f"         + {c['after']}")

    print(f"\n{tag}Summary: {len(replacer.changes)} replacement(s) "
          f"in {len(replacer.files_modified)} file(s).")

# ── Main ─────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Board retargeting for the GNSS ESP32 project.",
    )
    parser.add_argument(
        "--dry-run", action="store_true",
        help="Preview changes without writing to disk",
    )
    parser.add_argument(
        "--env-file", default=".env",
        help="Path to .env file relative to project root (default: .env)",
    )
    args = parser.parse_args()

    # Resolve project root (two levels up from utils/board/)
    script_dir = Path(__file__).resolve().parent
    root = script_dir.parent.parent

    env_path = root / args.env_file
    if not env_path.exists():
        sys.exit(
            f"[ERROR] .env file not found at {env_path}\n"
            f"  Copy .env.example to .env and edit it."
        )

    env = load_env(env_path)
    validate(env)

    print(f"TARGET_BOARD = {env['TARGET_BOARD']}")
    print(f"TARGET_CHIP  = {env['TARGET_CHIP']}")
    print(f"TARGET_LABEL = {env['TARGET_LABEL']}")
    print(f"TARGET_GNSS  = {env['TARGET_GNSS']}")
    print(f"Bootloader   = {BOOTLOADER_OFFSETS[env['TARGET_CHIP']]}")
    print(f"Chip label   = {chip_short_name(env['TARGET_CHIP'])}")

    replacer = FileReplacer(dry_run=args.dry_run)
    rules = build_rules(root, env)

    for filepath, file_rules in rules:
        if not filepath.exists():
            print(f"[WARN] File not found, skipping: {filepath}")
            continue
        replacer.process(filepath, file_rules)

    print_report(replacer, args.dry_run)


if __name__ == "__main__":
    main()
