"""
ESP32-C3 Build Flag Tester

Tests PlatformIO builds with various compiler flag combinations for ESP32-C3.
Validates that different build configurations compile successfully.

Usage:
    python build_Tester.py                  # Run all tests
    python build_Tester.py --tests 4,7,10   # Run tests 4, 7, and 10
    python build_Tester.py --tests 1-5      # Run tests 1 through 5
    python build_Tester.py --list           # List all available tests

Configuration:
    flags_to_tests.json     Test definitions with IDs, names, and flags
    platformio.ini.test     Minimal PlatformIO config (no build_flags)

How it works:
    1. Swaps platformio.ini with platformio.ini.test
    2. Runs each test with flags injected via PLATFORMIO_BUILD_FLAGS env var
    3. Restores original platformio.ini (even on failure/interrupt)
    4. Logs results to build_test_results.log

Exit codes:
    0 = All tests passed
    1 = One or more tests failed
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
    """Print all available tests."""
    print("\nAvailable tests:")
    print("-" * 60)
    for entry in test_flags:
        test_id = entry.get("id", "?")
        name = entry["name"]
        flags = " ".join(entry["flags"])
        print(f"  {test_id:>2}. {name}")
        print(f"      Flags: {flags}")
    print()


def main() -> int:
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="ESP32-C3 Build Flag Tester",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python build_Tester.py                  # Run all tests
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

    # Filter tests by ID if specified
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

    # Filter tests
    if selected_ids:
        test_flags = [t for t in all_tests if t.get("id") in selected_ids]
        if not test_flags:
            print(f"ERROR: No tests found with IDs: {selected_ids}")
            print("Use --list to see available tests.")
            return 1
    else:
        test_flags = all_tests

    # Find PlatformIO executable
    pio_cmd = find_platformio()
    print(f"\n=== ESP32-C3 Build Flag Tester ===")
    print(f"PlatformIO: {' '.join(pio_cmd)}")
    print(f"Environment: {env}")

    total_tests = len(test_flags)
    if selected_ids:
        print(f"Running tests: {sorted(selected_ids)}")
    logger.info(f"Running {total_tests} build tests for {env}.")
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

                result = run_platformio_build(
                    test_id, name, flags, env, i, total_tests, pio_cmd, logger
                )
                results.append(result)

                status = "[PASS]" if result[2] else "[FAIL]"
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

    passed = sum(1 for _, _, success, _ in results if success)
    failed = total_tests - passed

    print(f"\nPassed: {passed}/{total_tests}")
    print(f"Failed: {failed}/{total_tests}")

    if failed > 0:
        print("\n--- Failed Tests ---")
        for test_id, name, success, _ in results:
            if not success:
                print(f"  - #{test_id} {name}")

        logger.warning(
            "Some tests failed. Check for:\n"
            "- Memory overflow (reduce binary size with -Os or -flto)\n"
            "- Cache issues (add -mfix-esp32-c3-icache-issue)\n"
            "- Missing dependencies or undefined macros"
        )

    logger.info(f"Summary: {passed}/{total_tests} tests passed.")
    print(f"\nLog file: {LOG_FILE}")

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
