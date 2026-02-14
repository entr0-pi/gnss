"""
ESP32-C3 Build Flag Tester

Tests PlatformIO builds with various compiler flag combinations for ESP32-C3.
Validates that different build configurations compile successfully.

Usage:
    python build_Tester.py                  # Run all tests
    python build_Tester.py --tier A         # Run only smoke tests
    python build_Tester.py --tier A,B       # Run smoke + pairwise tests
    python build_Tester.py --tests 4,7,10   # Run tests 4, 7, and 10
    python build_Tester.py --tests 1-5      # Run tests 1 through 5
    python build_Tester.py --list           # List all available tests

Configuration:
    flags_to_tests.json     Test definitions with IDs, names, tiers, and flags
    platformio.ini.test     Minimal PlatformIO config (no build_flags)

Tiers:
    A   Smoke tests (bare default + full production config)
    B   Pairwise orthogonal combos + parameterization
    C   Negative/constraint tests (expected compile failures)

How it works:
    1. Swaps platformio.ini with platformio.ini.test
    2. Runs each test with flags injected via PLATFORMIO_BUILD_FLAGS env var
    3. Restores original platformio.ini (even on failure/interrupt)
    4. Logs results to build_test_results.log

Exit codes:
    0 = All tests met expectations
    1 = One or more tests did not meet expectations
"""

import argparse
import subprocess
import json
import logging
import os
import sys
import shutil
from pathlib import Path
from typing import List, Dict, Tuple, Optional, Set
from contextlib import contextmanager

# Get the script's directory and project root
SCRIPT_DIR = Path(__file__).parent.resolve()
PROJECT_ROOT = SCRIPT_DIR.parent.parent  # utils/build-tester -> utils -> project root

# PlatformIO ini file paths
PLATFORMIO_INI = PROJECT_ROOT / "platformio.ini"
PLATFORMIO_INI_STANDBY = PROJECT_ROOT / "platformio.ini.standby"
PLATFORMIO_TEST_INI = SCRIPT_DIR / "platformio.ini.test"

# Log file path
LOG_FILE = SCRIPT_DIR / "build_test_results.log"


def find_platformio() -> List[str]:
    """Find PlatformIO executable and return command prefix."""
    pio_path = shutil.which("pio")
    if pio_path:
        return [pio_path]

    home = Path.home()
    common_paths = [
        home / ".platformio" / "penv" / "Scripts" / "pio.exe",
        home / ".platformio" / "penv" / "Scripts" / "platformio.exe",
        home / "AppData" / "Local" / "Programs" / "Python" / "Python312" / "Scripts" / "pio.exe",
    ]
    for p in common_paths:
        if p.exists():
            return [str(p)]

    return [sys.executable, "-m", "platformio"]


def setup_logging() -> logging.Logger:
    """Configure logging with proper encoding handling."""
    logger = logging.getLogger("build_tester")
    logger.setLevel(logging.INFO)

    file_handler = logging.FileHandler(LOG_FILE, encoding="utf-8")
    file_handler.setLevel(logging.INFO)
    file_handler.setFormatter(logging.Formatter(
        "%(asctime)s - %(levelname)s - %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S"
    ))

    logger.addHandler(file_handler)
    return logger


@contextmanager
def swap_platformio_ini():
    """Context manager to swap platformio.ini with platformio.ini.test."""
    swapped = False
    try:
        if not PLATFORMIO_TEST_INI.exists():
            raise FileNotFoundError(f"Test ini not found: {PLATFORMIO_TEST_INI}")

        if PLATFORMIO_INI.exists():
            shutil.move(PLATFORMIO_INI, PLATFORMIO_INI_STANDBY)

        shutil.copy(PLATFORMIO_TEST_INI, PLATFORMIO_INI)
        swapped = True

        yield

    finally:
        if swapped:
            if PLATFORMIO_INI.exists():
                PLATFORMIO_INI.unlink()
            if PLATFORMIO_INI_STANDBY.exists():
                shutil.move(PLATFORMIO_INI_STANDBY, PLATFORMIO_INI)


def clean_build(env: str, pio_cmd: List[str]) -> bool:
    """Run PlatformIO clean to remove stale build artifacts."""
    cmd = pio_cmd + ["run", "-e", env, "-t", "clean"]
    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        cwd=PROJECT_ROOT
    )
    return result.returncode == 0


def run_platformio_build(
    test_id: int,
    name: str,
    flags: List[str],
    env: str,
    index: int,
    total: int,
    pio_cmd: List[str],
    logger: logging.Logger
) -> Tuple[int, str, bool, str]:
    """Run PlatformIO build with flags via environment variable."""
    progress = f"[{index}/{total}]"

    build_env = os.environ.copy()
    if flags:
        build_env["PLATFORMIO_BUILD_FLAGS"] = " ".join(flags)

    cmd = pio_cmd + ["run", "-e", env]

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            cwd=PROJECT_ROOT,
            env=build_env,
            timeout=300
        )
        success = result.returncode == 0
        error_output = result.stderr

        if "region `dram0_0_seg' overflowed" in error_output:
            logger.warning(f"{progress} Memory overflow in #{test_id} {name}.")
        if "icache" in error_output.lower():
            logger.warning(f"{progress} Cache issue in #{test_id} {name}.")

        if success:
            logger.info(f"{progress} PASS: #{test_id} {name}")
        else:
            truncated_error = error_output[:500] + "..." if len(error_output) > 500 else error_output
            logger.error(f"{progress} FAIL: #{test_id} {name}\nError:\n{truncated_error}")

        return (test_id, name, success, error_output if not success else "")

    except subprocess.TimeoutExpired:
        logger.error(f"{progress} TIMEOUT: #{test_id} {name} (exceeded 5 minutes)")
        return (test_id, name, False, "Build timed out")
    except Exception as e:
        logger.error(f"{progress} ERROR: #{test_id} {name}\nException: {str(e)}")
        return (test_id, name, False, str(e))


def parse_test_ids(test_arg: str) -> Set[int]:
    """Parse test IDs from comma or space separated string."""
    ids = set()
    for part in test_arg.replace(",", " ").split():
        try:
            ids.add(int(part))
        except ValueError:
            pass
    return ids


def list_tests(test_flags: List[Dict]) -> None:
    """Print all available tests grouped by tier."""
    print("\nAvailable tests:")
    print("-" * 60)
    current_tier = None
    for entry in test_flags:
        tier = entry.get("tier", "?")
        if tier != current_tier:
            current_tier = tier
            label = {"A": "Smoke", "B": "Pairwise + params", "C": "Negative (expect fail)"}.get(tier, tier)
            print(f"\n  Tier {tier}: {label}")
        test_id = entry.get("id", "?")
        name = entry["name"]
        expect = entry.get("expect", "pass")
        flags = " ".join(entry["flags"]) if entry["flags"] else "(none)"
        expect_tag = " [expect FAIL]" if expect == "fail" else ""
        print(f"    {test_id:>2}. {name}{expect_tag}")
        print(f"        Flags: {flags}")
    print()


def main() -> int:
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="ESP32-C3 Build Flag Tester",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python build_Tester.py                  # Run all tests
  python build_Tester.py --tier A         # Run only smoke tests
  python build_Tester.py --tier A,B       # Run smoke + pairwise
  python build_Tester.py --tests 4,7,10   # Run tests 4, 7, and 10
  python build_Tester.py --tests 1-5      # Run tests 1 through 5
  python build_Tester.py --list           # List all available tests
"""
    )
    parser.add_argument(
        "--tests", "-t",
        type=str,
        help="Comma-separated list of test IDs to run (e.g., 4,7,10 or 1-5)"
    )
    parser.add_argument(
        "--tier",
        type=str,
        help="Comma-separated tier letters to run (e.g., A or A,B or A,B,C)"
    )
    parser.add_argument(
        "--list", "-l",
        action="store_true",
        help="List all available tests and exit"
    )
    args = parser.parse_args()

    logger = setup_logging()
    config_path = SCRIPT_DIR / "flags_to_tests.json"

    try:
        with open(config_path, "r", encoding="utf-8") as f:
            config = json.load(f)
    except FileNotFoundError:
        print(f"ERROR: Config file not found: {config_path}")
        logger.error(f"Config file not found: {config_path}")
        return 1
    except json.JSONDecodeError as e:
        print(f"ERROR: Invalid JSON in config file: {e}")
        logger.error(f"Invalid JSON in config file: {e}")
        return 1

    all_tests = config["test_flags"]
    env = config["env"]

    # List tests and exit
    if args.list:
        list_tests(all_tests)
        return 0

    # Filter tests by tier and/or ID
    test_flags = all_tests

    if args.tier:
        selected_tiers = {t.strip().upper() for t in args.tier.split(",")}
        test_flags = [t for t in test_flags if t.get("tier", "").upper() in selected_tiers]
        if not test_flags:
            print(f"ERROR: No tests found for tier(s): {sorted(selected_tiers)}")
            print("Use --list to see available tests.")
            return 1

    selected_ids: Optional[Set[int]] = None
    if args.tests:
        # Handle range syntax (e.g., 1-5)
        if "-" in args.tests and "," not in args.tests:
            try:
                start, end = args.tests.split("-")
                selected_ids = set(range(int(start), int(end) + 1))
            except ValueError:
                selected_ids = parse_test_ids(args.tests)
        else:
            selected_ids = parse_test_ids(args.tests)

        test_flags = [t for t in test_flags if t.get("id") in selected_ids]
        if not test_flags:
            print(f"ERROR: No tests found with IDs: {selected_ids}")
            print("Use --list to see available tests.")
            return 1

    # Find PlatformIO executable
    pio_cmd = find_platformio()
    print(f"\n=== ESP32-C3 Build Flag Tester ===")
    print(f"PlatformIO: {' '.join(pio_cmd)}")
    print(f"Environment: {env}")

    total_tests = len(test_flags)
    tiers_in_run = sorted({t.get("tier", "?") for t in test_flags})
    if args.tier:
        print(f"Tiers: {', '.join(tiers_in_run)}")
    if selected_ids:
        print(f"Running tests: {sorted(selected_ids)}")
    logger.info(f"Running {total_tests} build tests for {env} (tiers: {', '.join(tiers_in_run)}).")
    print(f"Total tests: {total_tests}")
    print(f"Config: {config_path}\n")

    print("Swapping platformio.ini with platformio.ini.test...")

    try:
        with swap_platformio_ini():
            print("Cleaning build directory...")
            if not clean_build(env, pio_cmd):
                logger.warning("Initial clean failed, continuing anyway...")

            logger.info("Running tests sequentially...")
            print("Running tests sequentially...\n")

            results = []
            for i, entry in enumerate(test_flags, 1):
                test_id = entry.get("id", i)
                name = entry["name"]
                flags = entry["flags"]
                expect = entry.get("expect", "pass")

                result = run_platformio_build(
                    test_id, name, flags, env, i, total_tests, pio_cmd, logger
                )
                build_ok = result[2]

                if expect == "fail":
                    met = not build_ok
                    status = "[XFAIL]" if met else "[UNEX.PASS]"
                else:
                    met = build_ok
                    status = "[PASS]" if met else "[FAIL]"

                results.append((*result, expect, met))
                print(f"[{i}/{total_tests}] {status} #{test_id} {name}")

    except FileNotFoundError as e:
        print(f"ERROR: {e}")
        logger.error(str(e))
        return 1
    except Exception as e:
        print(f"ERROR during testing: {e}")
        logger.error(f"Exception during testing: {e}")
        return 1

    # Print summary
    print(f"\n{'=' * 50}")
    print("=== Build Test Results Summary ===")
    print(f"{'=' * 50}")

    met_count = sum(1 for *_, met in results if met)
    unmet_count = total_tests - met_count

    print(f"\nMet expectations: {met_count}/{total_tests}")
    if unmet_count:
        print(f"Did NOT meet expectations: {unmet_count}/{total_tests}")

        print("\n--- Unexpected Results ---")
        for test_id, name, build_ok, _, expect, met in results:
            if not met:
                if expect == "fail":
                    print(f"  - #{test_id} {name}  (expected compile failure, but it passed)")
                else:
                    print(f"  - #{test_id} {name}  (expected pass, but build failed)")

        logger.warning(
            "Some tests did not meet expectations. Check for:\n"
            "- Memory overflow (reduce binary size with -Os or -flto)\n"
            "- Cache issues (add -mfix-esp32-c3-icache-issue)\n"
            "- Missing dependencies or undefined macros\n"
            "- Constraint tests that unexpectedly passed (missing #error guard?)"
        )

    logger.info(f"Summary: {met_count}/{total_tests} tests met expectations.")
    print(f"\nLog file: {LOG_FILE}")

    return 0 if unmet_count == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
