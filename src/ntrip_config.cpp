#include "ntrip_config.h"

#include <Preferences.h>
#include "nvs_keys.h"

namespace {

// Preferences::putString returns strlen(value), which is 0 for empty strings
// even on success. This wrapper handles that edge case.
bool nvsPutString(Preferences& prefs, const char* key, const String& value) {
  size_t ret = prefs.putString(key, value);
  return (ret > 0) || value.isEmpty();
}

} // namespace

NtripConfig ntrip_config_defaults() {
  return NtripConfig{
    false,        // enabled
    "",           // host
    2101,         // port
    "",           // mount
    "",           // user
    "",           // pass
    5,            // max_tries
    30000,        // retry_delay_ms
    60000,        // health_timeout_ms
    5000,         // passive_sample_ms
    3,            // required_valid_frames
    1024,         // buffer_size
    5000          // connect_timeout_ms
  };
}

bool ntrip_config_validate(const NtripConfig& cfg, String* error) {
  if (cfg.host.isEmpty()) {
    if (error) *error = "host is required";
    return false;
  }
  if (cfg.mount.isEmpty()) {
    if (error) *error = "mount is required";
    return false;
  }
  if (cfg.port == 0) {
    if (error) *error = "port must be non-zero";
    return false;
  }
  if (cfg.max_tries < 1) {
    if (error) *error = "max_tries must be >= 1";
    return false;
  }
  if (cfg.buffer_size == 0) {
    if (error) *error = "buffer_size must be non-zero";
    return false;
  }
  if (cfg.connect_timeout_ms == 0) {
    if (error) *error = "connect_timeout_ms must be non-zero";
    return false;
  }
  return true;
}

bool ntrip_config_load(NtripConfig& out, NtripLockout* lockoutOut, String* error) {
  Preferences prefs;
  if (!prefs.begin(nvs_keys::ntrip::kNamespace, true)) {
    if (error) *error = "NVS open failed";
    return false;
  }

  const bool hasRequired =
      prefs.isKey(nvs_keys::ntrip::kEnabled) && prefs.isKey(nvs_keys::ntrip::kHost) &&
      prefs.isKey(nvs_keys::ntrip::kPort) && prefs.isKey(nvs_keys::ntrip::kMount) &&
      prefs.isKey(nvs_keys::ntrip::kUser) && prefs.isKey(nvs_keys::ntrip::kPass) &&
      prefs.isKey(nvs_keys::ntrip::kMaxTries) && prefs.isKey(nvs_keys::ntrip::kRetryDelay) &&
      prefs.isKey(nvs_keys::ntrip::kHealthTimeout) && prefs.isKey(nvs_keys::ntrip::kPassiveMs) &&
      prefs.isKey(nvs_keys::ntrip::kReqValid) && prefs.isKey(nvs_keys::ntrip::kBufferSize) &&
      prefs.isKey(nvs_keys::ntrip::kConnectTimeout);

  if (!hasRequired) {
    prefs.end();
    if (error) *error = "NTRIP config not found in NVS";
    return false;
  }

  out.enabled              = prefs.getBool(nvs_keys::ntrip::kEnabled);
  out.host                 = prefs.getString(nvs_keys::ntrip::kHost);
  out.port                 = (uint16_t)prefs.getUInt(nvs_keys::ntrip::kPort);
  out.mount                = prefs.getString(nvs_keys::ntrip::kMount);
  out.user                 = prefs.getString(nvs_keys::ntrip::kUser);
  out.pass                 = prefs.getString(nvs_keys::ntrip::kPass);
  out.max_tries            = prefs.getInt(nvs_keys::ntrip::kMaxTries);
  out.retry_delay_ms       = prefs.getULong(nvs_keys::ntrip::kRetryDelay);
  out.health_timeout_ms    = prefs.getULong(nvs_keys::ntrip::kHealthTimeout);
  out.passive_sample_ms    = prefs.getULong(nvs_keys::ntrip::kPassiveMs);
  out.required_valid_frames = prefs.getUInt(nvs_keys::ntrip::kReqValid);
  out.buffer_size          = prefs.getUInt(nvs_keys::ntrip::kBufferSize);
  out.connect_timeout_ms   = prefs.getULong(nvs_keys::ntrip::kConnectTimeout);

  if (lockoutOut) {
    lockoutOut->failed_attempts = prefs.getInt(nvs_keys::ntrip::lockout::kAttempts, 0);
    lockoutOut->abandoned       = prefs.getBool(nvs_keys::ntrip::lockout::kAbandoned, false);
    lockoutOut->last_config_hash = prefs.getString(nvs_keys::ntrip::lockout::kHash, "");
  }

  prefs.end();
  return true;
}

bool ntrip_config_save(const NtripConfig& in, const NtripLockout* lockoutToPreserve, String* error) {
  Preferences prefs;
  if (!prefs.begin(nvs_keys::ntrip::kNamespace, false)) {
    if (error) *error = "Failed to open NVS";
    return false;
  }

  bool ok = true;
  ok = ok && prefs.putBool(nvs_keys::ntrip::kEnabled, in.enabled);
  ok = ok && nvsPutString(prefs, nvs_keys::ntrip::kHost, in.host);
  ok = ok && prefs.putUInt(nvs_keys::ntrip::kPort, in.port);
  ok = ok && nvsPutString(prefs, nvs_keys::ntrip::kMount, in.mount);
  ok = ok && nvsPutString(prefs, nvs_keys::ntrip::kUser, in.user);
  ok = ok && nvsPutString(prefs, nvs_keys::ntrip::kPass, in.pass);
  ok = ok && prefs.putInt(nvs_keys::ntrip::kMaxTries, in.max_tries);
  ok = ok && prefs.putULong(nvs_keys::ntrip::kRetryDelay, in.retry_delay_ms);
  ok = ok && prefs.putULong(nvs_keys::ntrip::kHealthTimeout, in.health_timeout_ms);
  ok = ok && prefs.putULong(nvs_keys::ntrip::kPassiveMs, in.passive_sample_ms);
  ok = ok && prefs.putUInt(nvs_keys::ntrip::kReqValid, in.required_valid_frames);
  ok = ok && prefs.putUInt(nvs_keys::ntrip::kBufferSize, in.buffer_size);
  ok = ok && prefs.putULong(nvs_keys::ntrip::kConnectTimeout, in.connect_timeout_ms);

  if (lockoutToPreserve) {
    ok = ok && prefs.putInt(nvs_keys::ntrip::lockout::kAttempts, lockoutToPreserve->failed_attempts);
    ok = ok && prefs.putBool(nvs_keys::ntrip::lockout::kAbandoned, lockoutToPreserve->abandoned);
    ok = ok && nvsPutString(prefs, nvs_keys::ntrip::lockout::kHash, lockoutToPreserve->last_config_hash);
  }

  prefs.end();

  if (!ok) {
    if (error) *error = "Failed to write NVS";
    return false;
  }

  return true;
}
