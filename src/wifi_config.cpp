#include "wifi_config.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

namespace {
constexpr const char* kWifiConfigPath = "/wifi.json";

bool ensure_fs_ready(String* error) {
  if (LittleFS.begin(true)) {
    return true;
  }
  if (error) {
    *error = "LittleFS mount failed";
  }
  return false;
}

bool parse_ip_field(const JsonVariant& value, const char* field, IPAddress& out, String* error) {
  if (!value.is<const char*>()) {
    if (error) {
      *error = String("WiFi config missing or invalid ") + field;
    }
    return false;
  }

  const String ip_str = value.as<String>();
  if (!out.fromString(ip_str)) {
    if (error) {
      *error = String("WiFi config ") + field + " is not a valid IP: " + ip_str;
    }
    return false;
  }

  return true;
}
} // namespace

bool wifi_config_load(WifiConfig& cfg, String* error) {
  if (!ensure_fs_ready(error)) return false;

  if (!LittleFS.exists(kWifiConfigPath)) {
    if (error) {
      *error = String("WiFi config file not found: ") + kWifiConfigPath;
    }
    return false;
  }

  File file = LittleFS.open(kWifiConfigPath, "r");
  if (!file) {
    if (error) {
      *error = String("WiFi config open failed: ") + kWifiConfigPath;
    }
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    if (error) {
      *error = String("WiFi config JSON parse failed: ") + err.c_str();
    }
    return false;
  }

  if (!doc["ssid"].is<const char*>() || !doc["pass"].is<const char*>() ||
      !doc["dhcp"].is<bool>()) {
    if (error) {
      *error = "WiFi config requires ssid, pass, and dhcp";
    }
    return false;
  }

  cfg.ssid = doc["ssid"].as<String>();
  cfg.pass = doc["pass"].as<String>();
  cfg.dhcp = doc["dhcp"].as<bool>();
  if (cfg.ssid.isEmpty()) {
    if (error) {
      *error = "WiFi config ssid is empty";
    }
    return false;
  }

  if (!cfg.dhcp) {
    if (!parse_ip_field(doc["ip"], "ip", cfg.ip, error)) return false;
    if (!parse_ip_field(doc["gw"], "gw", cfg.gw, error)) return false;
    if (!parse_ip_field(doc["subnet"], "subnet", cfg.subnet, error)) return false;
    if (!parse_ip_field(doc["dns"], "dns", cfg.dns, error)) return false;
  } else {
    cfg.ip = IPAddress(0, 0, 0, 0);
    cfg.gw = IPAddress(0, 0, 0, 0);
    cfg.subnet = IPAddress(0, 0, 0, 0);
    cfg.dns = IPAddress(0, 0, 0, 0);
  }

  return true;
}
