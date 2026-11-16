See **ESP32 NVS Key-Pair Manager** (separate repo,
  [entr0-pi/arduino-nvs-desktop](https://github.com/entr0-pi/arduino-nvs-desktop)):
  a desktop app that reads and writes NVS values on a running board over USB
  serial, with no reflash and no LittleFS image. It needs `SERIAL_CONFIG_ENABLE=1`.
  Use it to change settings in the field or on the bench. Use `uploader/` to
  preload settings and web assets in production before sharing the firmware.
