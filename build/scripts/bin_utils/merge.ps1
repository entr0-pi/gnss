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

foreach ($f in @("bootloader.bin","partitions.bin","firmware.bin","data.bin")) {
    $path = Join-Path $BinDir $f
    if (-not (Test-Path $path)) {
        Write-Error "[ERROR] Missing: $path"
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

Write-Host "[INFO] Successfully created: $Output"
Write-Host "[INFO] To flash, run:"
Write-Host "[INFO]   python -m esptool --chip $Chip --port <PORT> write-flash 0x0 $Output"
Write-Host "[INFO]   (replace <PORT> with your device port, e.g. COM8)"
