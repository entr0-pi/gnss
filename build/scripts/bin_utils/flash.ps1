<#
.SYNOPSIS
Flash ESP32 binaries directly to device via serial port

.DESCRIPTION
Writes 4 binaries at their correct offsets with device reset

.PARAMETER Port
Serial port (default: COM8)

.PARAMETER Chip
ESP32 variant (default: esp32c3)

.PARAMETER Baud
Baud rate (default: 460800)

.PARAMETER SpiffsOffset
Data partition offset (substituted by docker-export-bins.sh)

.EXAMPLE
./flash.ps1 -Port COM4

.EXAMPLE
./flash.ps1 -Chip esp32s3 -Port COM8
#>

param(
    [string]$Port = "COM8",
    [string]$Chip = "esp32c3",
    [int]$Baud = 460800,
    [string]$SpiffsOffset = "__SPIFFS_OFFSET__"
)

# Validate required binaries exist
foreach ($f in @("bootloader.bin","partitions.bin","firmware.bin","data.bin")) {
    if (-not (Test-Path $f)) {
        Write-Error "[ERROR] Missing binary: $f (in $(Get-Location))" -ErrorAction Stop
        exit 1
    }
}

# Warn if serial port doesn't exist (but continue - may be available after reset)
if (-not (Test-Path $Port)) {
    Write-Warning "[WARN] Serial port not found: $Port (may appear after device reset)"
}

Write-Host "[INFO] Flashing to $Port ($Chip @ $Baud baud)"
python -m esptool --chip $Chip --port $Port --baud $Baud --before default-reset --after hard-reset write-flash -z `
  0x0 bootloader.bin `
  0x8000 partitions.bin `
  0x10000 firmware.bin `
  $SpiffsOffset data.bin

Write-Host "[SUCCESS] Flash complete. Device should reboot..."
