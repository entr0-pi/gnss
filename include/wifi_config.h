#pragma once

#include <Arduino.h>
#include <IPAddress.h>

struct WifiConfig {
  String ssid;
  String pass;
  bool dhcp = false;
  bool accesspoint = true;
  IPAddress ip;
  IPAddress gw;
  IPAddress subnet;
  IPAddress dns;
};

WifiConfig wifi_config_defaults();
bool wifi_config_validate(const WifiConfig& cfg, String* error);
bool wifi_config_load(WifiConfig& cfg, String* error);
bool wifi_config_save(const WifiConfig& cfg, String* error);
