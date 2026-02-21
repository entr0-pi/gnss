@echo off
REM Merge 4 ESP32 binaries into a single flashable image
REM Combines: bootloader, partitions, firmware, and data at their correct offsets
REM
REM Environment Variables:
REM   CHIP             ESP32 variant (default: esp32c3)
REM   BIN_DIR          Directory containing .bin files (default: build\bin)
REM   OUTPUT           Output gnss.bin path (default: %BIN_DIR%\gnss_%CHIP%.bin)
REM   SPIFFS_OFFSET    Data partition offset (default: 0x310000)
REM
REM Usage:
REM   merge.cmd                            REM Default: esp32c3 -> build\bin\gnss_esp32c3.bin
REM   set CHIP=esp32s3 && merge.cmd       REM Custom chip
REM   set OUTPUT=custom.bin && merge.cmd  REM Custom output path

setlocal enabledelayedexpansion

REM Set parameters with defaults
set "CHIP=!CHIP!"
if "!CHIP!"=="" set "CHIP=esp32c3"

set "BIN_DIR=!BIN_DIR!"
if "!BIN_DIR!"=="" set "BIN_DIR=build\bin"

set "OUTPUT=!OUTPUT!"
if "!OUTPUT!"=="" set "OUTPUT=!BIN_DIR!\gnss_!CHIP!.bin"

set "SPIFFS_OFFSET=!SPIFFS_OFFSET!"
if "!SPIFFS_OFFSET!"=="" set "SPIFFS_OFFSET=0x310000"

REM Create directory
if not exist "!BIN_DIR!" mkdir "!BIN_DIR!"

REM Validate required binaries exist
for %%f in (bootloader.bin partitions.bin firmware.bin data.bin) do (
    if not exist "!BIN_DIR!\%%f" (
        echo [ERROR] Missing binary: !BIN_DIR!\%%f
        exit /b 1
    )
)

echo [INFO] Merging binaries into !OUTPUT!
python -m esptool --chip !CHIP! merge-bin ^
  -o !OUTPUT! ^
  0x0 !BIN_DIR!\bootloader.bin ^
  0x8000 !BIN_DIR!\partitions.bin ^
  0x10000 !BIN_DIR!\firmware.bin ^
  !SPIFFS_OFFSET! !BIN_DIR!\data.bin

if errorlevel 1 (
  echo [ERROR] Merge failed
  exit /b 1
)

echo [SUCCESS] Created merged binary: !OUTPUT!
echo [INFO] To flash, run:
echo [INFO]   python -m esptool --chip !CHIP! --port ^<PORT^> write-flash 0x0 !OUTPUT!
echo [INFO]   ^(replace ^<PORT^> with your device port, e.g. COM8^)

endlocal
