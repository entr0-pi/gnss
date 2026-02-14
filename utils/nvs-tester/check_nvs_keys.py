#!/usr/bin/env python3
"""Check consistency between nvs_keys.h and nvs_keys.csv files.

Parses the C++ header to extract NVS namespace/key declarations,
parses the CSV to extract the actual NVS partition layout,
and reports any mismatches.

Usage:
    python utils/nvs-tester/check_nvs_keys.py

    Run from anywhere — paths are resolved relative to the repo root.
    Checks both nvs_keys.csv and nvs_keys.csv.example against include/nvs_keys.h.

    Exit code 0: all files are consistent.
    Exit code 1: mismatches found or a required file is missing.
"""

import csv
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
H_FILE = REPO_ROOT / "include" / "nvs_keys.h"
CSV_FILE = REPO_ROOT / "utils" / "uploader" / "nvs_keys.csv"
CSV_EXAMPLE = REPO_ROOT / "utils" / "uploader" / "nvs_keys.csv.example"


def parse_header(path: Path) -> dict[str, set[str]]:
    """Parse nvs_keys.h and return {nvs_namespace: {key, ...}}."""
    text = path.read_text(encoding="utf-8")

    # Track C++ namespace nesting to resolve which NVS namespace a key belongs to.
    # When we see kNamespace = "xxx", that sets the NVS namespace for the current scope.
    # Sub-namespaces (like ntrip::lockout) inherit the parent's NVS namespace.
    result: dict[str, set[str]] = {}
    nvs_ns_stack: list[str | None] = []  # one entry per brace depth
    current_nvs_ns: str | None = None

    lines = text.splitlines()
    brace_depth = 0

    for line in lines:
        stripped = line.strip()

        # Track namespace openings: "namespace foo {"
        ns_match = re.match(r"namespace\s+(\w+)\s*\{", stripped)
        if ns_match:
            nvs_ns_stack.append(current_nvs_ns)
            brace_depth += 1
            continue

        # Track closing braces (with optional comment)
        if stripped.startswith("}"):
            if nvs_ns_stack:
                current_nvs_ns = nvs_ns_stack.pop()
                brace_depth -= 1
            continue

        # kNamespace = "xxx" -> sets the NVS namespace for this scope
        ns_val = re.match(
            r'constexpr\s+const\s+char\*\s+kNamespace\s*=\s*"([^"]+)"', stripped
        )
        if ns_val:
            current_nvs_ns = ns_val.group(1)
            result.setdefault(current_nvs_ns, set())
            continue

        # kSomething = "xxx" -> a key under current NVS namespace
        key_val = re.match(
            r'constexpr\s+const\s+char\*\s+\w+\s*=\s*"([^"]+)"', stripped
        )
        if key_val and current_nvs_ns is not None:
            result[current_nvs_ns].add(key_val.group(1))

    return result


def parse_csv(path: Path) -> dict[str, set[str]]:
    """Parse an NVS CSV and return {nvs_namespace: {key, ...}}."""
    result: dict[str, set[str]] = {}
    current_ns: str | None = None

    with path.open(encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            key = row["key"].strip()
            typ = row["type"].strip()
            if typ == "namespace":
                current_ns = key
                result.setdefault(current_ns, set())
            elif typ == "data" and current_ns is not None:
                result[current_ns].add(key)

    return result


def compare(
    h_data: dict[str, set[str]],
    csv_data: dict[str, set[str]],
    csv_label: str,
) -> list[str]:
    """Compare header vs CSV and return a list of issue strings."""
    issues: list[str] = []

    all_ns = sorted(set(h_data.keys()) | set(csv_data.keys()))
    for ns in all_ns:
        h_keys = h_data.get(ns, set())
        c_keys = csv_data.get(ns, set())

        if ns not in h_data:
            issues.append(f"  namespace '{ns}': in {csv_label} but NOT in .h")
            continue
        if ns not in csv_data:
            issues.append(f"  namespace '{ns}': in .h but NOT in {csv_label}")
            continue

        only_h = sorted(h_keys - c_keys)
        only_c = sorted(c_keys - h_keys)

        for k in only_h:
            issues.append(f"  [{ns}] key '{k}': in .h but NOT in {csv_label}")
        for k in only_c:
            issues.append(f"  [{ns}] key '{k}': in {csv_label} but NOT in .h")

    return issues


def main() -> int:
    if not H_FILE.exists():
        print(f"ERROR: {H_FILE} not found")
        return 1

    h_data = parse_header(H_FILE)

    all_issues: list[str] = []

    for csv_path in (CSV_FILE, CSV_EXAMPLE):
        label = csv_path.name
        if not csv_path.exists():
            all_issues.append(f"  SKIP: {label} not found")
            continue

        csv_data = parse_csv(csv_path)
        issues = compare(h_data, csv_data, label)
        all_issues.extend(issues)

    if all_issues:
        print("\u274c NVS keys mismatch:")
        for i in all_issues:
            print(i)
        return 1

    print("\u2705 NVS keys are consistent")
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(main())
