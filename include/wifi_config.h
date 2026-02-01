#pragma once

#include <Arduino.h>
#include <IPAddress.h>

struct WifiConfig {
  String ssid;
  String pass;
  bool dhcp = false;
  IPAddress ip;
  IPAddress gw;
  IPAddress subnet;
  IPAddress dns;
};

// Loads WiFi configuration from LittleFS /wifi.json.
// Returns true when the config is loaded and valid.
// On failure, returns false and populates error (if provided).
bool wifi_config_load(WifiConfig& cfg, String* error);

// Saves WiFi configuration to LittleFS /wifi.json.
// Returns true when the config is saved.
// On failure, returns false and populates error (if provided).
bool wifi_config_save(const WifiConfig& cfg, String* error);
