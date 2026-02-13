#pragma once

#include <Arduino.h>

struct GnssConfig {
  int rx_pin;
  int tx_pin;
  uint32_t baud;
};

GnssConfig gnss_config_defaults();
bool gnss_config_validate(const GnssConfig& cfg, String* error);
bool gnss_config_load(GnssConfig& out, String* error);
bool gnss_config_save(const GnssConfig& cfg, String* error);

// Applies the config to the UART and persists it (implemented in main.cpp).
bool gnss_apply_runtime_config(const GnssConfig& cfg, String* error);
