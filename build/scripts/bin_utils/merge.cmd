@echo off
setlocal enabledelayedexpansion

REM Default values
set "CHIP=%CHIP:~0%"
if "!CHIP!"=="" set "CHIP=esp32c3"

set "BIN_DIR=%BIN_DIR:~0%"
if "!BIN_DIR!"=="" set "BIN_DIR=build\bin"

set "OUTPUT=%OUTPUT:~0%"
if "!OUTPUT!"=="" set "OUTPUT=!BIN_DIR!\gnss_!CHIP!.bin"

set "SPIFFS_OFFSET=%SPIFFS_OFFSET:~0%"
if "!SPIFFS_OFFSET!"=="" set "SPIFFS_OFFSET=0x310000"

REM Create directory
if not exist "!BIN_DIR!" mkdir "!BIN_DIR!"

REM Validate files
for %%f in (bootloader.bin partitions.bin firmware.bin data.bin) do (
    if not exist "!BIN_DIR!\%%f" (
        echo [ERROR] Missing: !BIN_DIR!\%%f
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

if errorlevel 1 exit /b 1

echo [INFO] Successfully created: !OUTPUT!
echo [INFO] To flash, run:
echo [INFO]   python -m esptool --chip !CHIP! --port ^<PORT^> write-flash 0x0 !OUTPUT!
echo [INFO]   ^(replace ^<PORT^> with your device port, e.g. COM8^)

endlocal
