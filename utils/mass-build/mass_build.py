#!/usr/bin/env python3
"""Mass build: build firmware for every board entry using the Docker flow.

Default boards.json holds one reference build per supported chip and USB type
(esp32, esp32s3_uart, esp32s3_usb, esp32c3_uart, esp32c3_usb).
Optional BUILD_FLAGS are passed as PLATFORMIO_BUILD_FLAGS. <name> is the entry's NAME,
or TARGET_BOARD when NAME is absent.

For each entry:
  1. Retarget the working tree (utils/board/retarget.py rules)
  2. docker build  -t gnss-mass:<name> -f build/Dockerfile .
  3. docker run    -v build/bin/<name>:/project/build gnss-mass:<name>

All files touched by retargeting are snapshotted first and restored
byte-for-byte at the end (also on failure or Ctrl+C).

Usage:
    python utils/mass-build/mass_build.py                         # boards.json
    python utils/mass-build/mass_build.py --boards my_boards.json
    python utils/mass-build/mass_build.py --env-files .env.example1 .env.example2
    python utils/mass-build/mass_build.py --only esp32c3_usb esp32c3_uart
    python utils/mass-build/mass_build.py --boards utils/mass-build/boards_all.json
    python utils/mass-build/mass_build.py --dry-run               # print plan only
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT = SCRIPT_DIR.parent.parent

sys.path.insert(0, str(ROOT / "utils" / "board"))
import retarget  # noqa: E402  (utils/board/retarget.py)

BASE_IMAGE = "gnss-base"
IMAGE_REPO = "gnss-mass"
REQUIRED_KEYS = ("TARGET_BOARD", "TARGET_CHIP", "TARGET_LABEL", "TARGET_GNSS")


def build_name(board: dict) -> str:
    """Optional NAME (output folder / image tag), defaults to TARGET_BOARD."""
    return board.get("NAME") or board["TARGET_BOARD"]

# ── Board list loading ───────────────────────────────────────────────

def load_boards_json(path: Path) -> list[dict]:
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    boards = data.get("boards", data) if isinstance(data, dict) else data
    if not isinstance(boards, list):
        sys.exit(f"[ERROR] {path}: expected a list of boards (or {{\"boards\": [...]}})")
    return boards


def load_boards(args) -> list[dict]:
    if args.env_files:
        boards = [retarget.load_env((ROOT / p).resolve()) for p in args.env_files]
    else:
        boards = load_boards_json((ROOT / args.boards).resolve())

    for i, b in enumerate(boards):
        missing = [k for k in REQUIRED_KEYS if not b.get(k)]
        if missing:
            sys.exit(f"[ERROR] Board #{i + 1} is missing: {', '.join(missing)}")
        err = retarget.check_chip(b["TARGET_CHIP"])
        if err:
            sys.exit(f"[ERROR] Board '{build_name(b)}': {err}")

    names = [build_name(b) for b in boards]
    dupes = sorted({n for n in names if names.count(n) > 1})
    if dupes:
        sys.exit(f"[ERROR] Duplicate NAME/TARGET_BOARD entries: {', '.join(dupes)}")

    if args.only:
        unknown = set(args.only) - set(names)
        if unknown:
            sys.exit(f"[ERROR] --only: unknown board(s): {', '.join(sorted(unknown))}")
        boards = [b for b in boards if build_name(b) in args.only]
    return boards

# ── Working-tree snapshot ────────────────────────────────────────────

def retarget_files() -> list[Path]:
    """All files retarget.py may modify (paths do not depend on env values)."""
    dummy = {"TARGET_BOARD": "x", "TARGET_CHIP": "esp32",
             "TARGET_LABEL": "x", "TARGET_GNSS": "x"}
    return [p for p, _ in retarget.build_rules(ROOT, dummy)]


class Snapshot:
    """Save file bytes and restore them exactly (line endings included)."""

    def __init__(self, paths: list[Path]):
        self.saved = {p: p.read_bytes() for p in paths if p.exists()}

    def restore(self) -> None:
        for p, data in self.saved.items():
            if p.read_bytes() != data:
                p.write_bytes(data)

# ── Docker helpers ───────────────────────────────────────────────────

def image_tag(board: str) -> str:
    """Docker tags: lowercase [a-z0-9_.-], max 128 chars."""
    tag = re.sub(r"[^a-z0-9_.-]", "-", board.lower()).strip(".-") or "board"
    return f"{IMAGE_REPO}:{tag[:128]}"


def run(cmd: list[str], log, dry_run: bool) -> int:
    """Run a command, tee output to console and log file."""
    line = "$ " + " ".join(f'"{c}"' if " " in c else c for c in cmd)
    print(line)
    if log:
        log.write(line + "\n")
        log.flush()
    if dry_run:
        return 0
    env = dict(os.environ, DOCKER_BUILDKIT="1")  # Dockerfiles use --mount=type=cache
    proc = subprocess.Popen(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True,
                            encoding="utf-8", errors="replace")
    for out in proc.stdout:
        sys.stdout.write(out)
        if log:
            log.write(out)
    return proc.wait()


def image_exists(name: str) -> bool:
    return subprocess.run(["docker", "image", "inspect", name],
                          stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode == 0


def check_docker() -> None:
    if not shutil.which("docker"):
        sys.exit("[ERROR] 'docker' not found in PATH.")
    if subprocess.run(["docker", "info"], stdout=subprocess.DEVNULL,
                      stderr=subprocess.DEVNULL).returncode != 0:
        sys.exit("[ERROR] Docker daemon is not reachable. Is Docker Desktop running?")

# ── Per-board build ──────────────────────────────────────────────────

def apply_retarget(board: dict, dry_run: bool) -> None:
    replacer = retarget.FileReplacer(dry_run=dry_run)
    for path, rules in retarget.build_rules(ROOT, board):
        if path.exists():
            replacer.process(path, rules)
        else:
            print(f"[WARN] File not found, skipping: {path}")
    print(f"[INFO] Retarget: {len(replacer.changes)} replacement(s) "
          f"in {len(replacer.files_modified)} file(s)")


def build_board(board: dict, out_root: Path, args) -> tuple[bool, str]:
    name = build_name(board)
    chip = board["TARGET_CHIP"]
    tag = image_tag(name)
    out_dir = out_root / name

    if not args.dry_run:
        if out_dir.exists():
            shutil.rmtree(out_dir)
        out_dir.mkdir(parents=True)
    log_path = out_dir / "build.log"
    log = None if args.dry_run else open(log_path, "w", encoding="utf-8")

    try:
        apply_retarget(board, args.dry_run)

        build_cmd = ["docker", "build", "-t", tag, "-f", "build/Dockerfile"]
        if args.no_cache:
            build_cmd.append("--no-cache")
        build_cmd.append(".")
        if run(build_cmd, log, args.dry_run) != 0:
            return False, "docker build failed"

        run_cmd = ["docker", "run", "--rm", "-e", f"CHIP={chip}"]
        if board.get("BUILD_FLAGS"):
            # PlatformIO appends PLATFORMIO_BUILD_FLAGS to build_flags
            run_cmd += ["-e", f"PLATFORMIO_BUILD_FLAGS={board['BUILD_FLAGS']}"]
        run_cmd += ["-v", f"{out_dir}:/project/build", tag]
        if run(run_cmd, log, args.dry_run) != 0:
            return False, "docker run failed"

        if not args.dry_run:
            merged = out_dir / f"gnss_{chip}.bin"
            if not merged.exists():
                return False, f"missing {merged.name}"
            (out_dir / "board.json").write_text(
                json.dumps(board, indent=2) + "\n", encoding="utf-8")

        if args.prune_images:
            run(["docker", "image", "rm", tag], log, args.dry_run)
        return True, "ok"
    finally:
        if log:
            log.close()

# ── Main ─────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Build firmware for every TARGET_BOARD via Docker.")
    src = parser.add_mutually_exclusive_group()
    src.add_argument("--boards", default="utils/mass-build/boards.json",
                     help="JSON board list, relative to project root "
                          "(default: utils/mass-build/boards.json)")
    src.add_argument("--env-files", nargs="+", metavar="FILE",
                     help=".env-style files (one board each), relative to project root")
    parser.add_argument("--only", nargs="+", metavar="BOARD",
                        help="Build only these NAME (or TARGET_BOARD) entries")
    parser.add_argument("--out", default="build/bin",
                        help="Output root, one subfolder per board (default: build/bin)")
    parser.add_argument("--rebuild-base", action="store_true",
                        help=f"Rebuild the '{BASE_IMAGE}' image even if it exists")
    parser.add_argument("--no-cache", action="store_true",
                        help="Pass --no-cache to the per-board docker build")
    parser.add_argument("--prune-images", action="store_true",
                        help=f"Remove each {IMAGE_REPO}:<board> image after export")
    parser.add_argument("--fail-fast", action="store_true",
                        help="Stop at the first failing board")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print the plan and commands; modify nothing, run nothing")
    args = parser.parse_args()

    boards = load_boards(args)
    if not boards:
        sys.exit("[ERROR] No boards to build.")
    out_root = (ROOT / args.out).resolve()

    tag = "[DRY RUN] " if args.dry_run else ""
    print(f"{tag}Boards ({len(boards)}):")
    for b in boards:
        print(f"  - {build_name(b):<16} {b['TARGET_BOARD']:<24} {b['TARGET_CHIP']:<9} "
              f"boot@{retarget.BOOTLOADER_OFFSETS[b['TARGET_CHIP']]:<7} {b['TARGET_LABEL']}"
              + (f"  [+{b['BUILD_FLAGS']}]" if b.get("BUILD_FLAGS") else ""))
    print(f"{tag}Output: {out_root}\\<board>\\" if os.name == "nt"
          else f"{tag}Output: {out_root}/<board>/")

    if not args.dry_run:
        check_docker()

    # Base image (heavy toolchains), built once
    if args.rebuild_base or args.dry_run or not image_exists(BASE_IMAGE):
        if run(["docker", "build", "-t", BASE_IMAGE, "-f", "build/Dockerfile.base", "."],
               None, args.dry_run) != 0:
            sys.exit(f"[ERROR] Failed to build {BASE_IMAGE}")
    else:
        print(f"[INFO] Reusing existing '{BASE_IMAGE}' image (use --rebuild-base to refresh)")

    snapshot = Snapshot(retarget_files())
    results: list[tuple[str, bool, str, float]] = []
    try:
        for i, board in enumerate(boards, 1):
            name = build_name(board)
            print(f"\n{'=' * 70}\n{tag}[{i}/{len(boards)}] {name} "
                  f"({board['TARGET_CHIP']})\n{'=' * 70}")
            t0 = time.monotonic()
            try:
                ok, msg = build_board(board, out_root, args)
            except Exception as e:  # keep going, report at the end
                ok, msg = False, f"{type(e).__name__}: {e}"
            results.append((name, ok, msg, time.monotonic() - t0))
            print(f"[{'SUCCESS' if ok else 'ERROR'}] {name}: {msg}")
            if not ok and args.fail_fast:
                break
    except KeyboardInterrupt:
        print("\n[WARN] Interrupted.")
    finally:
        snapshot.restore()
        print("\n[INFO] Working tree restored to its pre-build state.")

    # Summary
    print(f"\n{tag}Summary")
    print(f"  {'BOARD':<24} {'RESULT':<8} {'TIME':>7}  DETAIL")
    for name, ok, msg, dt in results:
        print(f"  {name:<24} {'OK' if ok else 'FAIL':<8} {dt:>6.0f}s  {msg}")
    skipped = len(boards) - len(results)
    if skipped:
        print(f"  ({skipped} board(s) not attempted)")
    failed = sum(1 for _, ok, _, _ in results if not ok)
    print(f"\n{len(results) - failed}/{len(boards)} board(s) built successfully.")
    sys.exit(1 if failed or skipped else 0)


if __name__ == "__main__":
    main()
