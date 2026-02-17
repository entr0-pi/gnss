#include "config_gnss.h"

#include <Preferences.h>
#include "app.h"
#include "nvs_keys.h"

static_assert(NVS_SCHEMA_VERSION == 2,
              "GNSS NVS schema mismatch: update firmware or schema");
static_assert(NVS_GNSS_REQUIRED_KEYS == 3,
              "GNSS NVS key count mismatch: update firmware or schema");

GnssConfig gnss_config_defaults() {
  GnssConfig cfg;
#if IMMUTABLE_UART
  cfg.rx_pin = PIN_GNSS_RX;
  cfg.tx_pin = PIN_GNSS_TX;
  cfg.baud   = GNSS_BAUD;
#else
  cfg.rx_pin = -1;
  cfg.tx_pin = -1;
  cfg.baud   = 0;
#endif
  return cfg;
}

bool gnss_config_validate(const GnssConfig& cfg, String* error) {
  const bool rxUncfg   = (cfg.rx_pin == -1);
  const bool txUncfg   = (cfg.tx_pin == -1);
  const bool baudUncfg = (cfg.baud == 0);

  // All-or-nothing: either fully unconfigured or fully configured
  if (rxUncfg || txUncfg || baudUncfg) {
    if (rxUncfg && txUncfg && baudUncfg) return true;
    if (error) *error = "rx_pin, tx_pin, and baud must all be set or all be -1/-1/0.";
    return false;
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
  if (error) *error = "GNSS config is immutable (build flags)";
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
    if (error) *error = "GNSS config write failed";
    return false;
  }

  return true;
#endif
}
