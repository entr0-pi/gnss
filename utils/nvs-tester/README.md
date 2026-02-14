# NVS Key Consistency Checker

Small validation utility that checks whether NVS keys declared in firmware code are consistent with the CSV files used to generate the NVS partition.

## What this tool does

`check_nvs_keys.py` parses:

- `include/nvs_keys.h` (source-of-truth constants used by firmware)
- `utils/uploader/nvs_keys.csv` (runtime/working CSV)
- `utils/uploader/nvs_keys.csv.example` (example/template CSV)

It then compares namespaces and keys and reports mismatches (missing namespaces, missing keys, extra keys).

## How to use

Run from anywhere inside the repo:

```bash
python utils/nvs-tester/check_nvs_keys.py
```

### Exit codes

- `0`: all checked files are consistent
- `1`: mismatches found or required files are missing

## Key features

- **Namespace-aware header parsing**: follows nested C++ namespace scopes and `kNamespace` assignments.
- **CSV parsing with namespace sections**: understands `type=namespace` and `type=data` rows.
- **Dual-file validation**: checks both real CSV and example CSV in one run.
- **CI-friendly behavior**: deterministic output and non-zero exit code when validation fails.
