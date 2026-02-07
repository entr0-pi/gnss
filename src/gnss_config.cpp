#include "gnss_config.h"

#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#include "app.h"

namespace {
constexpr const char* kGnssNvsNs = "gnss";
constexpr const char* kGnssJsonPath = "/gnss.json";

bool g_nvs_ready = false;
GnssConfig g_config{};

bool load_config_nvs(GnssConfig& cfg) {
  if (!g_nvs_ready) return false;

  Preferences prefs;
  if (!prefs.begin(kGnssNvsNs, true)) return false;

  if (!prefs.isKey("rx_pin") || !prefs.isKey("tx_pin") || !prefs.isKey("baud")) {
    prefs.end();
    return false;
  }

  cfg.rx_pin = prefs.getInt("rx_pin", cfg.rx_pin);
  cfg.tx_pin = prefs.getInt("tx_pin", cfg.tx_pin);
  cfg.baud = prefs.getULong("baud", cfg.baud);

  prefs.end();
  return true;
}

bool load_config_littlefs(GnssConfig& cfg, String* error) {
  if (!LittleFS.begin(false)) {
    if (error) *error = "LittleFS mount failed";
    return false;
  }

  File file = LittleFS.open(kGnssJsonPath, "r");
  if (!file) {
    if (error) *error = "gnss.json not found";
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    if (error) *error = String("gnss.json parse error: ") + err.c_str();
    return false;
  }

  if (!doc["rx_pin"].is<int>() || !doc["tx_pin"].is<int>() || !doc["baud"].is<uint32_t>()) {
    if (error) *error = "gnss.json missing rx_pin/tx_pin/baud";
    return false;
  }

  cfg.rx_pin = doc["rx_pin"].as<int>();
  cfg.tx_pin = doc["tx_pin"].as<int>();
  cfg.baud = doc["baud"].as<uint32_t>();
  return true;
}
} // namespace

GnssConfig gnss_config_defaults() {
  return GnssConfig{PIN_GNSS_RX, PIN_GNSS_TX, GNSS_BAUD};
}

bool gnss_config_validate(const GnssConfig& cfg, String* error) {
  // Allow -1/0 as "unconfigured" state
  if (cfg.rx_pin == -1 || cfg.tx_pin == -1 || cfg.baud == 0) {
    return true; // Unconfigured is valid (will skip UART init)
  }

  if (cfg.rx_pin < 0 || cfg.tx_pin < 0) {
    if (error) *error = "Pins must be -1 (unconfigured) or non-negative.";
    return false;
  }
  if (cfg.rx_pin == cfg.tx_pin) {
    if (error) *error = "RX and TX pins must be different.";
    return false;
  }
  if (cfg.baud < 1200 || cfg.baud > 2000000) {
    if (error) *error = "Baud rate must be 0 (unconfigured) or between 1200 and 2000000.";
    return false;
  }
  return true;
}

bool gnss_config_begin() {
  g_config = gnss_config_defaults();

  Preferences prefs;
  if (!prefs.begin(kGnssNvsNs, false)) {
    Serial.println("[GNSS] NVS open failed");
    Serial.println("[GNSS] Using fallback hardcoded config");
    g_nvs_ready = false;
    g_config = GnssConfig{FALLBACK_GNSS_RX, FALLBACK_GNSS_TX, FALLBACK_GNSS_BAUD};
    return true;
  }
  prefs.end();

  g_nvs_ready = true;
  Serial.println("[GNSS] NVS ready");

  // Precedence: NVS first. Use /gnss.json only when NVS is empty/invalid.
  GnssConfig loaded_nvs = g_config;
  if (load_config_nvs(loaded_nvs) && gnss_config_validate(loaded_nvs, nullptr)) {
    g_config = loaded_nvs;
    Serial.println("[GNSS] Config loaded from NVS");
  } else {
    Serial.println("[GNSS] NVS config missing/invalid, trying /gnss.json");
    GnssConfig loaded_fs = g_config;
    String fs_error;
    if (load_config_littlefs(loaded_fs, &fs_error) && gnss_config_validate(loaded_fs, nullptr)) {
      g_config = loaded_fs;
      Serial.println("[GNSS] Config loaded from /gnss.json");
      if (!gnss_config_save(g_config)) {
        Serial.println("[GNSS] Warning: Failed to sync /gnss.json to NVS");
      }
    } else {
      Serial.println(String("[GNSS] /gnss.json not used: ") + fs_error);
      Serial.println("[GNSS] Creating NVS config with defaults");
      if (gnss_config_save(g_config)) {
        Serial.println("[GNSS] Default config saved successfully");
      } else {
        Serial.println("[GNSS] Warning: Failed to save default config");
      }
    }
  }

  return true;
}

const GnssConfig& gnss_config_get() {
  return g_config;
}

bool gnss_config_save(const GnssConfig& cfg) {
  if (!g_nvs_ready) return false;

  Preferences prefs;
  if (!prefs.begin(kGnssNvsNs, false)) return false;

  bool ok = true;
  ok = ok && prefs.putInt("rx_pin", cfg.rx_pin) > 0;
  ok = ok && prefs.putInt("tx_pin", cfg.tx_pin) > 0;
  ok = ok && prefs.putULong("baud", cfg.baud) > 0;

  prefs.end();

  if (!ok) return false;

  g_config = cfg;
  return true;
}
