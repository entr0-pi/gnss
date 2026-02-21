@echo off
set PORT=%1
if "%PORT%"=="" set PORT=COM8
set CHIP=%CHIP%
if "%CHIP%"=="" set CHIP=esp32c3
set BAUD=%BAUD%
if "%BAUD%"=="" set BAUD=460800
set SPIFFS_OFFSET=%SPIFFS_OFFSET%
if "%SPIFFS_OFFSET%"=="" set SPIFFS_OFFSET=__SPIFFS_OFFSET__

for %%f in (bootloader.bin partitions.bin firmware.bin data.bin) do (
  if not exist %%f (echo [ERROR] Missing: %%f & exit /b 1)
)

python -m esptool --chip %CHIP% --port %PORT% --baud %BAUD% --before default-reset --after hard-reset write-flash -z ^
  0x0 bootloader.bin ^
  0x8000 partitions.bin ^
  0x10000 firmware.bin ^
  %SPIFFS_OFFSET% data.bin
