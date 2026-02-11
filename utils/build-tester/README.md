# ESP32-C3 Build Flag Tester

Tests PlatformIO builds with various compiler flag combinations for the ESP32-C3, validating that different build configurations compile successfully.

## Files

| File | Description |
|------|-------------|
| `build_Tester.py` | Test runner script |
| `flags_to_tests.json` | Test definitions (IDs, names, tiers, flags) |
| `platformio.ini.test` | Minimal PlatformIO config (no `build_flags`) |
| `Dockerfile` | Reproducible Docker build environment |

## Test matrix

Tests are organized in three tiers:

| Tier | Purpose | Count | Description |
|------|---------|-------|-------------|
| **A** | Smoke | 2 | Bare default (no flags) + full production config |
| **B** | Pairwise + params | 10 | Orthogonal feature combos + parameter variations |
| **C** | Negative | 2 | Constraint violations that must fail to compile |

**Tier A + B** together provide complete pairwise coverage of the 5 binary feature toggles (`BLE_ENABLE`, `WEBUI_ENABLE`, `NMEA_ENABLE`, `TCP_ENABLE`, `NTRIP_CLIENT_ENABLE`), respecting the constraints:
- `NTRIP_CLIENT_ENABLE` requires `WEBUI_ENABLE` (hard `#error`)
- `NMEA_ENABLE` requires `WEBUI_ENABLE` (soft — silently forced to 0)

**Tier C** validates that the `#error` guards actually fire on invalid combos.

## Quick start (local)

Requires PlatformIO installed on the host.

```bash
# Run from the project root
python utils/build-tester/build_Tester.py              # all tests
python utils/build-tester/build_Tester.py --tier A      # smoke only
python utils/build-tester/build_Tester.py --tier A,B    # smoke + pairwise
python utils/build-tester/build_Tester.py --tests 0,10  # specific test IDs
python utils/build-tester/build_Tester.py --list        # list all tests
```

`--tier` and `--tests` can be combined to filter within a tier.

## Quick start (Docker)

No host dependencies required beyond Docker.

```bash
# Build the image (run from the project root)
docker build -t gnss-build-tester -f utils/build-tester/Dockerfile .

# Run all tests
docker run --rm gnss-build-tester

# Run only smoke tests
docker run --rm gnss-build-tester --tier A

# List available tests
docker run --rm gnss-build-tester --list
```

To extract the log file from a run:

```bash
docker run --rm -v ./results:/project/utils/build-tester gnss-build-tester
# Log available at ./results/build_test_results.log
```

## How it works

1. `platformio.ini` is temporarily swapped with `platformio.ini.test` (a stripped-down config with no `build_flags`).
2. Each test injects its flags via the `PLATFORMIO_BUILD_FLAGS` environment variable.
3. The original `platformio.ini` is restored after all tests complete (even on failure/interrupt).
4. Results are logged to `build_test_results.log`.

When running inside Docker, the container starts with `platformio.ini.test` already in place as `platformio.ini`, so the swap is a harmless no-op.

## Result labels

| Label | Meaning |
|-------|---------|
| `[PASS]` | Build succeeded (expected) |
| `[FAIL]` | Build failed unexpectedly |
| `[XFAIL]` | Build failed as expected (Tier C) |
| `[UNEX.PASS]` | Build succeeded but was expected to fail |

## Exit codes

| Code | Meaning |
|------|---------|
| `0` | All tests met expectations |
| `1` | One or more tests did not meet expectations |

## Adding a test

Edit `flags_to_tests.json` and append an entry to the appropriate tier:

```json
{"id": 22, "tier": "B", "name": "My New Config", "flags": ["-DMY_FLAG=1"]}
```

For a negative test (expected compile failure):

```json
{"id": 23, "tier": "C", "name": "Bad combo (must fail)", "flags": ["-DBAD=1"], "expect": "fail"}
```
