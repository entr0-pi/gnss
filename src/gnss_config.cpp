#include "gnss_config.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "app.h"

namespace {
constexpr const char* kConfigPath = "/gnss.json";

bool g_fs_ready = false;
GnssConfig g_config{};

bool load_config_file(GnssConfig& cfg) {
  if (!g_fs_ready) return false;
  if (!LittleFS.exists(kConfigPath)) return false;

  File file = LittleFS.open(kConfigPath, "r");
  if (!file) return false;

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) return false;

  if (doc["rx_pin"].is<int>()) cfg.rx_pin = doc["rx_pin"].as<int>();
  if (doc["tx_pin"].is<int>()) cfg.tx_pin = doc["tx_pin"].as<int>();
  if (doc["baud"].is<uint32_t>()) cfg.baud = doc["baud"].as<uint32_t>();

  return true;
}
} // namespace

GnssConfig gnss_config_defaults() {
  return GnssConfig{PIN_GNSS_RX, PIN_GNSS_TX, GNSS_BAUD};
}

bool gnss_config_validate(const GnssConfig& cfg, String* error) {
  if (cfg.rx_pin < 0 || cfg.tx_pin < 0) {
    if (error) *error = "Pins must be non-negative.";
    return false;
  }
  if (cfg.rx_pin == cfg.tx_pin) {
    if (error) *error = "RX and TX pins must be different.";
    return false;
  }
  if (cfg.baud < 1200 || cfg.baud > 2000000) {
    if (error) *error = "Baud rate must be between 1200 and 2000000.";
    return false;
  }
  return true;
}

bool gnss_config_begin() {
  g_fs_ready = LittleFS.begin(true);
  g_config = gnss_config_defaults();

  if (!g_fs_ready) return false;

  GnssConfig loaded = g_config;
  if (load_config_file(loaded) && gnss_config_validate(loaded, nullptr)) {
    g_config = loaded;
  }

  return true;
}

const GnssConfig& gnss_config_get() {
  return g_config;
}

bool gnss_config_save(const GnssConfig& cfg) {
  if (!g_fs_ready) return false;

  File file = LittleFS.open(kConfigPath, "w");
  if (!file) return false;

  JsonDocument doc;
  doc["rx_pin"] = cfg.rx_pin;
  doc["tx_pin"] = cfg.tx_pin;
  doc["baud"] = cfg.baud;

  const size_t written = serializeJson(doc, file);
  file.close();

  if (written == 0) return false;

  g_config = cfg;
  return true;
}
