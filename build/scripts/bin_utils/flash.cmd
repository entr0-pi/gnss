@echo off
REM Flash ESP32 binaries directly to device via serial port
REM Writes 4 binaries at their correct offsets with device reset
REM
REM Arguments:
REM   %1           Serial port (optional, default: COM8)
REM
REM Environment Variables:
REM   CHIP         ESP32 variant (default: esp32c3)
REM   PORT         Serial port (default: %1 or COM8)
REM   BAUD         Baud rate (default: 460800)
REM   SPIFFS_OFFSET Data partition offset (substituted by docker-export-bins.sh)
REM
REM Usage:
REM   flash.cmd                    REM Default port COM8
REM   flash.cmd COM4              REM Custom port as argument
REM   set PORT=COM4 && flash.cmd  REM Custom port via environment

setlocal enabledelayedexpansion

REM Parse parameters and environment variables
set "PORT=%1"
if "!PORT!"=="" set "PORT=!PORT_ENV!"
if "!PORT!"=="" set "PORT=COM8"

set "CHIP=!CHIP_ENV!"
if "!CHIP!"=="" set "CHIP=esp32c3"

set "BAUD=!BAUD_ENV!"
if "!BAUD!"=="" set "BAUD=460800"

set "SPIFFS_OFFSET=!SPIFFS_OFFSET_ENV!"
if "!SPIFFS_OFFSET!"=="" set "SPIFFS_OFFSET=__SPIFFS_OFFSET__"

REM Validate required binaries exist
for %%f in (bootloader.bin partitions.bin firmware.bin data.bin) do (
  if not exist %%f (
    echo [ERROR] Missing binary: %%f ^(in %CD%^)
    exit /b 1
  )
)

echo [INFO] Flashing to !PORT! ^(!CHIP! @ !BAUD! baud^)
python -m esptool --chip !CHIP! --port !PORT! --baud !BAUD! --before default-reset --after hard-reset write-flash -z ^
  0x0 bootloader.bin ^
  0x8000 partitions.bin ^
  0x10000 firmware.bin ^
  !SPIFFS_OFFSET! data.bin

if errorlevel 1 (
  echo [ERROR] Flash failed
  exit /b 1
)

echo [SUCCESS] Flash complete. Device should reboot...
