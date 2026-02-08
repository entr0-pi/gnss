#pragma once

#include "app.h"

#if BLE_ENABLE
#include <NimBLEDevice.h>

void menuToolSetup(NimBLEServer* server);
void menuToolTick();
#endif
