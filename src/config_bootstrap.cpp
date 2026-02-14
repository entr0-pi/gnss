#include "config_bootstrap.h"

#include <Preferences.h>

#include "app.h"
#include "nvs_keys.h"
#include "gnss_config.h"
#include "wifi_config.h"
#include "ntrip_config.h"
#define MODULE_LOG 1
#include "logger.h"

// Build-time schema enforcement.
static_assert(NVS_SCHEMA_VERSION == 2,
              "NVS schema version mismatch: firmware and NVS schema incompatible");

namespace {

bool gnss_nvs_has_data() {
  Preferences prefs;
  if (!prefs.begin(nvs_keys::gnss::kNamespace, true)) return false;
  bool has = prefs.isKey(nvs_keys::gnss::kRxPin) &&
             prefs.isKey(nvs_keys::gnss::kTxPin) &&
             prefs.isKey(nvs_keys::gnss::kBaud);
  prefs.end();
  return has;
}

bool wifi_nvs_has_data() {
  Preferences prefs;
  if (!prefs.begin(nvs_keys::wifi::kNamespace, true)) return false;
  bool has = prefs.isKey(nvs_keys::wifi::kSsid) &&
             prefs.isKey(nvs_keys::wifi::kPass) &&
             prefs.isKey(nvs_keys::wifi::kDhcp) &&
             prefs.isKey(nvs_keys::wifi::kIp) &&
             prefs.isKey(nvs_keys::wifi::kGw) &&
             prefs.isKey(nvs_keys::wifi::kSubnet) &&
             prefs.isKey(nvs_keys::wifi::kDns) &&
             prefs.isKey(nvs_keys::wifi::kAccessPoint);
  prefs.end();
  return has;
}

bool ntrip_nvs_has_data() {
  Preferences prefs;
  if (!prefs.begin(nvs_keys::ntrip::kNamespace, true)) return false;
  bool has = prefs.isKey(nvs_keys::ntrip::kEnabled) &&
             prefs.isKey(nvs_keys::ntrip::kHost) &&
             prefs.isKey(nvs_keys::ntrip::kPort) &&
             prefs.isKey(nvs_keys::ntrip::kMount) &&
             prefs.isKey(nvs_keys::ntrip::kUser) &&
             prefs.isKey(nvs_keys::ntrip::kPass) &&
             prefs.isKey(nvs_keys::ntrip::kMaxTries) &&
             prefs.isKey(nvs_keys::ntrip::kRetryDelay) &&
             prefs.isKey(nvs_keys::ntrip::kHealthTimeout) &&
             prefs.isKey(nvs_keys::ntrip::kPassiveMs) &&
             prefs.isKey(nvs_keys::ntrip::kReqValid) &&
             prefs.isKey(nvs_keys::ntrip::kBufferSize) &&
             prefs.isKey(nvs_keys::ntrip::kConnectTimeout) &&
             prefs.isKey(nvs_keys::ntrip::lockout::kAttempts) &&
             prefs.isKey(nvs_keys::ntrip::lockout::kAbandoned) &&
             prefs.isKey(nvs_keys::ntrip::lockout::kHash);
  prefs.end();
  return has;
}

}  // namespace

void config_bootstrap() {
  LOG_I("BOOT", "NVS bootstrap (schema v%d)", NVS_SCHEMA_VERSION);

  // ---- GNSS / UART ----
#if !IMMUTABLE_UART
  if (!gnss_nvs_has_data()) {
    LOG_I("BOOT", "GNSS NVS empty, seeding defaults");
    GnssConfig defaults = gnss_config_defaults();
    if (!gnss_config_save(defaults, nullptr)) {
      LOG_E("BOOT", "Failed to seed GNSS defaults");
    }
  } else {
    LOG_I("BOOT", "GNSS NVS already populated");
  }
#else
  LOG_I("BOOT", "GNSS config immutable (build flags)");
#endif

  // ---- WiFi ----
#if !IMMUTABLE_WIFI
  if (!wifi_nvs_has_data()) {
    LOG_I("BOOT", "WiFi NVS empty, seeding defaults");
    WifiConfig defaults= wifi_config_defaults();
    if (!wifi_config_save(defaults, nullptr)) {
      LOG_E("BOOT", "Failed to seed WiFi defaults");
    }
    // Seed the accesspoint key (managed outside wifi_config module).
    Preferences prefs;
    if (prefs.begin(nvs_keys::wifi::kNamespace, false)) {
      prefs.putBool(nvs_keys::wifi::kAccessPoint, true);
      prefs.end();
    }
  } else {
    LOG_I("BOOT", "WiFi NVS already populated");
  }
#else
  LOG_I("BOOT", "WiFi config immutable (build flags or WiFi disabled)");
#endif

  // ---- NTRIP ----
#if !IMMUTABLE_NTRIP
  if (!ntrip_nvs_has_data()) {
    LOG_I("BOOT", "NTRIP NVS empty, seeding defaults");
    NtripConfig defaults = ntrip_config_defaults();
    NtripLockout lockout_defaults{0, false, ""};
    if (!ntrip_config_save(defaults, &lockout_defaults, nullptr)) {
      LOG_E("BOOT", "Failed to seed NTRIP defaults");
    }
  } else {
    LOG_I("BOOT", "NTRIP NVS already populated");
  }
#else
  LOG_I("BOOT", "NTRIP config immutable (NTRIP disabled)");
#endif

  LOG_I("BOOT", "NVS bootstrap complete");
}
