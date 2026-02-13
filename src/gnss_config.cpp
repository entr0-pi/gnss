#include "gnss_config.h"

#include <Preferences.h>

#include "app.h"
#include "nvs_keys.h"
#define MODULE_LOG 1
#include "logger.h"

static_assert(NVS_SCHEMA_VERSION == 1,
              "GNSS NVS schema mismatch: update firmware or schema");
static_assert(NVS_GNSS_REQUIRED_KEYS == 3,
              "GNSS NVS key count mismatch: update firmware or schema");

GnssConfig gnss_config_defaults() {
  #if IMMUTABLE_UART
    return GnssConfig{
      PIN_GNSS_RX,
      PIN_GNSS_TX,
      GNSS_BAUD
    };
  #else
    return GnssConfig{
      -1, // rx_pin
      -1, // tx_pin
      0   // baud
    };
  #endif
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

bool gnss_config_load(GnssConfig& out, String* error) {
  Preferences prefs;
  if (!prefs.begin(nvs_keys::gnss::kNamespace, true)) {
    if (error) *error = "NVS open failed";
    return false;
  }

  if (!prefs.isKey(nvs_keys::gnss::kRxPin) ||
      !prefs.isKey(nvs_keys::gnss::kTxPin) ||
      !prefs.isKey(nvs_keys::gnss::kBaud)) {
    prefs.end();
    if (error) *error = "GNSS config not found in NVS";
    return false;
  }

  out.rx_pin = prefs.getInt(nvs_keys::gnss::kRxPin);
  out.tx_pin = prefs.getInt(nvs_keys::gnss::kTxPin);
  out.baud = prefs.getULong(nvs_keys::gnss::kBaud);

  prefs.end();
  return true;
}

bool gnss_config_save(const GnssConfig& cfg, String* error) {
#if IMMUTABLE_UART
  (void)cfg;
  if (error) *error = "UART config is immutable (build flags)";
  return false;
#else
  Preferences prefs;
  if (!prefs.begin(nvs_keys::gnss::kNamespace, false)) {
    if (error) *error = "NVS open failed";
    return false;
  }

  bool ok = true;
  ok = ok && prefs.putInt(nvs_keys::gnss::kRxPin, cfg.rx_pin) > 0;
  ok = ok && prefs.putInt(nvs_keys::gnss::kTxPin, cfg.tx_pin) > 0;
  ok = ok && prefs.putULong(nvs_keys::gnss::kBaud, cfg.baud) > 0;

  prefs.end();

  if (!ok) {
    if (error) *error = "Failed to write NVS";
    return false;
  }

  return true;
#endif
}
