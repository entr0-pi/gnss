#pragma once

#include <Arduino.h>

struct NtripConfig {
  bool enabled;
  String host;
  uint16_t port;
  String mount;
  String user;
  String pass;
  int max_tries;
  uint32_t retry_delay_ms;
  uint32_t health_timeout_ms;
  uint32_t passive_sample_ms;
  uint32_t required_valid_frames;
  uint32_t buffer_size;
  uint32_t connect_timeout_ms;
  bool send_gga;
};

struct NtripLockout {
  int failed_attempts;
  bool abandoned;
  String last_config_hash;
};

NtripConfig ntrip_config_defaults();
bool ntrip_config_validate(const NtripConfig& cfg, String* error);
bool ntrip_config_load(NtripConfig& out, NtripLockout* lockoutOut, String* error);
bool ntrip_config_save(const NtripConfig& in, const NtripLockout* lockoutToPreserve, String* error);
