---
name: gnss-release-binary-packager
description: Produce release-ready ESP32 binary bundles (bootloader, partitions, firmware), include flash offsets, and provide operator-ready flashing instructions.
---

# GNSS Release Binary Packager

Use this skill for release preparation and manufacturing handoff.

## Workflow

1. Build firmware for target environment.
2. Collect binary artifacts:
   - `bootloader.bin`
   - `partitions.bin`
   - `firmware.bin`
3. Include standard offsets in release notes:
   - `0x1000`, `0x8000`, `0x10000`
4. Add board/chip compatibility notes.
5. Include checksum manifest and a short flashing guide.

## Deliverables

- versioned release folder
- flash command examples
- integrity checklist
