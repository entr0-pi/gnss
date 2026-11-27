#pragma once

#include <stddef.h>

// USB-serial NVS config protocol used by the desktop tool (docs/SERIAL.md).
// Compiled only with SERIAL_CONFIG_ENABLE=1 (see app.h).

// RX buffer size the protocol needs; call Serial.setRxBufferSize() with it
// BEFORE Serial.begin().
constexpr size_t SERIAL_CONFIG_RX_BUFFER = 1024;

/// Resets the protocol session (unsynced, empty reply cache). Opens no NVS
/// handle: namespaces are opened per command. Call once in setup(), after
/// config_bootstrap().
void serial_config_setup();

/// Drains Serial and handles complete frames. Call from loop().
void serial_config_poll();
