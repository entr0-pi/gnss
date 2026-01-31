#pragma once

#include <Arduino.h>

struct Um980Config {
  int rx_pin;
  int tx_pin;
  uint32_t baud;
};

bool um980_config_begin();
const Um980Config& um980_config_get();
Um980Config um980_config_defaults();
bool um980_config_validate(const Um980Config& cfg, String* error);
bool um980_config_save(const Um980Config& cfg);

// Applies the config to the UART and persists it (implemented in main.cpp).
bool um980_apply_runtime_config(const Um980Config& cfg, String* error);
