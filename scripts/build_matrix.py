#!/usr/bin/env python3
"""Proposed build-matrix runner for PlatformIO environments."""

from __future__ import annotations

import argparse
import subprocess
from dataclasses import dataclass
from typing import Iterable, Sequence


@dataclass(frozen=True)
class MatrixEntry:
    name: str
    description: str
    flags: Sequence[str]


MATRIX: tuple[MatrixEntry, ...] = (
    MatrixEntry(
        name="default",
        description="Baseline build with web UI and TCP enabled.",
        flags=("WEBUI_ENABLE=1", "TCP_ENABLE=1", "NMEA_ENABLE=0"),
    ),
    MatrixEntry(
        name="webui_off",
        description="Headless build that skips web asset generation.",
        flags=("WEBUI_ENABLE=0",),
    ),
    MatrixEntry(
        name="nmea",
        description="Enable the NMEA parser for parsing/telemetry.",
        flags=("NMEA_ENABLE=1",),
    ),
    MatrixEntry(
        name="tcp_off",
        description="Disable the TCP mirror to minimize network usage.",
        flags=("TCP_ENABLE=0",),
    ),
    MatrixEntry(
        name="high_mtu",
        description="Increase BLE MTU for larger notify payloads.",
        flags=("BLE_MTU_CFG=185",),
    ),
    MatrixEntry(
        name="custom_name",
        description="Override BLE advertising name for field deployments.",
        flags=('BLE_DEVICE_NAME="GNSS-FIELD"',),
    ),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Proposed build matrix runner for PlatformIO envs."
    )
    parser.add_argument(
        "--execute",
        action="store_true",
        help="Run builds instead of only printing the commands.",
    )
    parser.add_argument(
        "--env",
        action="append",
        default=[],
        help="Limit to specific environment(s). Can be repeated.",
    )
    return parser.parse_args()


def iter_matrix(names: Iterable[str]) -> Iterable[MatrixEntry]:
    if not names:
        return MATRIX
    selected = {name.strip() for name in names if name.strip()}
    return tuple(entry for entry in MATRIX if entry.name in selected)


def build_commands(entries: Iterable[MatrixEntry]) -> list[list[str]]:
    return [["pio", "run", "-e", entry.name] for entry in entries]


def main() -> int:
    args = parse_args()
    entries = iter_matrix(args.env)
    commands = build_commands(entries)

    for command, entry in zip(commands, entries, strict=False):
        print(f"# {entry.name}: {entry.description}")
        print(f"# Flags: {', '.join(entry.flags)}")
        print("+ " + " ".join(command))
        if args.execute:
            subprocess.run(command, check=True)
        print()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
