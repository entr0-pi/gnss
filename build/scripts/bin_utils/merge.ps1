<#
.SYNOPSIS
Merge 4 ESP32 binaries into a single flashable image

.DESCRIPTION
Combines bootloader, partitions, firmware, and data at their correct offsets

.PARAMETER Chip
ESP32 variant (default: esp32c3)

.PARAMETER BinDir
Directory containing .bin files (default: build/bin)

.PARAMETER Output
Output gnss.bin path (default: {BinDir}/gnss_{Chip}.bin)

.PARAMETER SpiffsOffset
Data partition offset (default: 0x310000)

.EXAMPLE
./merge.ps1

.EXAMPLE
./merge.ps1 -Chip esp32s3 -Output custom.bin
#>

param(
    [string]$Chip = "esp32c3",
    [string]$BinDir = "build/bin",
    [string]$Output = "",
    [string]$SpiffsOffset = "0x310000"
)

if ([string]::IsNullOrEmpty($Output)) {
    $Output = Join-Path $BinDir "gnss_${Chip}.bin"
}

New-Item -ItemType Directory -Path $BinDir -Force -ErrorAction SilentlyContinue | Out-Null

# Validate required binaries exist
foreach ($f in @("bootloader.bin","partitions.bin","firmware.bin","data.bin")) {
    $path = Join-Path $BinDir $f
    if (-not (Test-Path $path)) {
        Write-Error "[ERROR] Missing binary: $path" -ErrorAction Stop
        exit 1
    }
}

Write-Host "[INFO] Merging binaries into $Output"
python -m esptool --chip $Chip merge-bin `
  -o $Output `
  0x0 (Join-Path $BinDir "bootloader.bin") `
  0x8000 (Join-Path $BinDir "partitions.bin") `
  0x10000 (Join-Path $BinDir "firmware.bin") `
  $SpiffsOffset (Join-Path $BinDir "data.bin")

Write-Host "[SUCCESS] Created merged binary: $Output"
Write-Host "[INFO] To flash, run:"
Write-Host "[INFO]   python -m esptool --chip $Chip --port <PORT> write-flash 0x0 $Output"
Write-Host "[INFO]   (replace <PORT> with your device port, e.g. COM8)"
