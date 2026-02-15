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

KNOWN_CHIPS = {
    "esp32", "esp32s2", "esp32s3",
    "esp32c2", "esp32c3", "esp32c6",
    "esp32h2", "esp32p4",
}

BOOTLOADER_OFFSETS = {
    "esp32":   "0x1000",
    "esp32s2": "0x1000",
    "esp32s3": "0x0",
    "esp32c2": "0x0",
    "esp32c3": "0x0",
    "esp32c6": "0x0",
    "esp32h2": "0x0",
    "esp32p4": "0x0",
}

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
    if env["TARGET_CHIP"] not in KNOWN_CHIPS:
        sys.exit(
            f"[ERROR] TARGET_CHIP='{env['TARGET_CHIP']}' is not recognised.\n"
            f"  Known chips: {', '.join(sorted(KNOWN_CHIPS))}"
        )

# ── Replacement engine ───────────────────────────────────────────────

class FileReplacer:
    """Apply regex replacements and track what changed."""

    def __init__(self, dry_run: bool):
        self.dry_run = dry_run
        self.changes: list[dict] = []
        self.files_modified: set[str] = set()

    def process(self, filepath: Path, rules: list[tuple[re.Pattern, str]]) -> None:
        text = filepath.read_text(encoding="utf-8")
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
                filepath.write_text("".join(lines), encoding="utf-8")

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

        # ── 3. uploader default chip ────────────────────────────────
        (root / "utils" / "uploader" / "uploaderGUI.py", [
            (re.compile(r'(chip_var\s*=\s*tk\.StringVar\(value=")\w+(")'),
             rf"\g<1>{C}\g<2>"),
        ]),

        # ── 4. docker-export-bins.sh (flash script templates) ───────
        (root / "build" / "scripts" / "docker-export-bins.sh", [
            # Bash: CHIP="${CHIP:-esp32c3}"
            (re.compile(r'(CHIP="\$\{CHIP:-)\w+(\}")'),
             rf"\g<1>{C}\g<2>"),
            # PowerShell: [string]$Chip = "esp32c3"
            (re.compile(r'(\[string\]\$Chip\s*=\s*")\w+(")'),
             rf"\g<1>{C}\g<2>"),
            # CMD: if "%CHIP%"=="" set CHIP=esp32c3
            (re.compile(r"(set CHIP=)\w+", re.MULTILINE),
             rf"\g<1>{C}"),
            # Bootloader offset in flash commands (all three templates)
            (re.compile(r"^(\s*)0x[0-9a-fA-F]+(\s+bootloader\.bin)", re.MULTILINE),
             rf"\g<1>{O}\g<2>"),
        ]),

        # ── 5. Web UI index.html ────────────────────────────────────
        (root / "data" / "web" / "index.html", [
            (re.compile(r"(<title>).*?(</title>)"),
             rf"\g<1>{CSN} Status\g<2>"),
            (re.compile(r'(class="sub">).*?(</div>)'),
             rf"\g<1>Web Server on {Le} with {Ge}\g<2>"),
        ]),

        # ── 6. Web UI app.js ────────────────────────────────────────
        (root / "data" / "web" / "app.js", [
            (re.compile(r'(confirm\("Restart ).*?( now\?"\))'),
             rf"\g<1>{CSN}\g<2>"),
        ]),

        # ── 7. INSTALL.md (bootloader offset) ───────────────────────
        (root / "INSTALL.md", [
            (re.compile(r"(`bootloader\.bin`\s*->\s*`)0x[0-9a-fA-F]+"),
             rf"\g<1>{O}"),
        ]),

        # ── 8. docs/DIAGRAM.md ──────────────────────────────────────
        (root / "docs" / "DIAGRAM.md", [
            # Line 3: "> ESP32-C3 GNSS RTK system ..."
            (re.compile(r"^(>\s*)\S+(\s+GNSS RTK)", re.MULTILINE),
             rf"\g<1>{CSN}\g<2>"),
            # Line 82: subgraph ESP["ESP32-C3"]
            (re.compile(r'(subgraph ESP\[").*?("\])'),
             rf"\g<1>{CSN}\g<2>"),
            # Line 732: Board: **Lolin C3 Mini** (...)
            (re.compile(r"(Board:\s*\*\*).*?(\*\*)"),
             rf"\g<1>{Le}\g<2>"),
        ]),

        # ── 9. build-tester README.md ────────────────────────────────
        (root / "utils" / "build-tester" / "README.md", [
            # Title: "# ESP32-C3 Build Flag Tester"
            (re.compile(r"^(#\s*)\S+(\s+Build Flag Tester)", re.MULTILINE),
             rf"\g<1>{CSN}\g<2>"),
            # Body: "for the ESP32-C3, validating..."
            (re.compile(r"(for the\s+)[\w\-]+(,?\s+validating)"),
             rf"\g<1>{CSN}\g<2>"),
        ]),

        # ── 10. build-tester build_Tester.py ─────────────────────────
        (root / "utils" / "build-tester" / "build_Tester.py", [
            # Docstring / help / print: "ESP32-C3 Build Flag Tester"
            (re.compile(r"ESP32-\S+(\s+Build Flag Tester)"),
             rf"{CSN}\g<1>"),
            # Docstring: "for ESP32-C3."
            (re.compile(r"(for\s+)ESP32-[\w\-]+(\.?)"),
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
