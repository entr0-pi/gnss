param(
    [string]$Port = "COM8",
    [string]$Chip = "esp32c3",
    [int]$Baud = 460800,
    [string]$SpiffsOffset = "0x310000"
)

python -m esptool --chip $Chip --port $Port --baud $Baud --before default-reset --after hard-reset write-flash -z `
  0x0 bootloader.bin `
  0x8000 partitions.bin `
  0x10000 firmware.bin `
  $SpiffsOffset data.bin
