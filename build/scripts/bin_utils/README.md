# Binary Utility Scripts (`bin_utils/`)

Platform-agnostic utility scripts for building and flashing GNSS firmware binaries.

## Architecture & Design Principles

### 1. Environment Variable Defaults
All scripts follow a consistent pattern:
```bash
VAR="${VAR:-default_value}"
```

- **create-data-bin.sh**: Uses `ROOT_DIR`, `DATA_DIR`, `WEB_DIR`, `OUT_BIN`, `MKLITTLEFS`
- **flash.sh/ps1/cmd**: Uses `CHIP`, `PORT/HOST`, `BAUD`, `SPIFFS_OFFSET`

### 2. Error Handling
Consistent error reporting across all platforms:
```
[ERROR] message
[WARN] message
[INFO] message
[SUCCESS] message
```

Exit codes:
- `0` = success
- `1` = fatal error (missing files, invalid config, tool not found)

### 3. File Validation
All scripts validate required files BEFORE execution:
1. Check if files exist
2. Check if directories are accessible
3. Report clear, actionable error messages
4. Exit with error before attempting operations

### 4. Parameter Handling

#### Bash Scripts
- Use environment variables for configuration
- Provide sensible defaults
- Support both positional args (flash.sh <PORT>) and env vars (CHIP=esp32s3)
- No interactive prompts

#### PowerShell Scripts
- Use named parameters with defaults
- Support both parameter syntax and environment variables
- Use `Write-Error` / `Write-Host` for output
- Include optional warnings for non-critical issues

#### Batch Scripts
- Use environment variables (CMD has no true parameters)
- Provide defaults with `if "%VAR%"=="" set VAR=...`
- Use `echo` for output with `[INFO]`, `[ERROR]`, `[WARN]` prefixes
- Support quoted values: `set "CHIP=..."`

### 5. Logging/Output Format

All scripts use tagged output:
- `[INFO]`: Informational messages
- `[ERROR]`: Errors (always to stderr in bash/ps1)
- `[WARN]`: Warnings (non-fatal issues)
- `[SUCCESS]`: Final success message

### 6. Assumptions & Constraints

**create-data-bin.sh:**
- Runs inside Docker container OR on host with mklittlefs installed
- Reads from `${ROOT_DIR}/data/` and `${ROOT_DIR}/data/web/`
- Generates `data.bin` at specified output path
- Compresses CSS/HTML/JS files with gzip

**flash.sh / flash.ps1 / flash.cmd:**
- Reads from current directory (where binaries are located)
- Uses esptool to write directly to device
- Requires pyserial and esptool packages installed
- Available for manual flashing if needed

## Script Cross-Reference

| Script | Purpose | Input | Output | Dependencies |
|--------|---------|-------|--------|--------------|
| `create-data-bin.sh` | Build LittleFS data image | `data/`, `data/web/` | `data.bin` | mklittlefs, gzip, awk |
| `flash.sh` | Flash to device (Bash) | `*.bin` files | Direct write to device | esptool, python |
| `flash.ps1` | Flash to device (PowerShell) | `*.bin` files | Direct write to device | esptool, python |
| `flash.cmd` | Flash to device (Batch) | `*.bin` files | Direct write to device | esptool, python |

## Environment Variables Reference

### Global (all scripts)
- `CHIP`: ESP32 variant (default: `esp32c3`)

### create-data-bin.sh
- `ROOT_DIR`: Project root (default: derived from script location)
- `DATA_DIR`: Data directory (default: `${ROOT_DIR}/data`)
- `WEB_DIR`: Web assets directory (default: `${ROOT_DIR}/data/web`)
- `PARTITIONS_CSV`: Partition table (default: `${ROOT_DIR}/partitions.csv`)
- `OUT_BIN`: Output data.bin path (default: `${ROOT_DIR}/build/bin/data.bin`)
- `MKLITTLEFS`: Path to mklittlefs tool (auto-detected if not set)

### flash.sh
- `PORT`: Serial port (default: `/dev/ttyUSB0`)
- `BAUD`: Baud rate (default: `460800`)
- `SPIFFS_OFFSET`: Data partition offset (substituted by docker-export-bins.sh)

### flash.ps1
- `Port`: Serial port (default: `COM8`)
- `Baud`: Baud rate (default: `460800`)
- `SpiffsOffset`: Data partition offset (substituted by docker-export-bins.sh)

### flash.cmd
- `PORT`: Serial port (default: `COM8`)
- `BAUD`: Baud rate (default: `460800`)
- `SPIFFS_OFFSET`: Data partition offset (substituted by docker-export-bins.sh)

## Testing

Each script should be tested with:
1. Valid inputs → successful execution
2. Missing required files → clear error message and exit 1
3. Invalid ports/chips → handled gracefully
4. Default values → work correctly

## Maintenance Notes

- **SPIFFS_OFFSET**: Substituted at Docker export time (don't hardcode in generated copies)
- **Platform differences**: Bash/PS1/CMD are semantically equivalent but use platform idioms
- **Error messages**: Always include what was expected vs. what was found
- **Success messages**: Report what was created/modified for user clarity
