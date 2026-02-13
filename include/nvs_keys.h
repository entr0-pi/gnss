#pragma once

// NVS schema version. Increment when adding/removing/renaming keys.
// Every config module asserts against this at build time.
#define NVS_SCHEMA_VERSION        1

// Required key counts per namespace.
// These must match the actual keys read/written by each *_config module.
#define NVS_GNSS_REQUIRED_KEYS    3   // rx_pin, tx_pin, baud
#define NVS_WIFI_REQUIRED_KEYS    8   // ssid, pass, dhcp, ip, gw, subnet, dns, accesspoint
#define NVS_NTRIP_REQUIRED_KEYS  16   // enabled..connect_timeout + lockout (3)

namespace nvs_keys {

namespace gnss {
constexpr const char* kNamespace = "gnss";
constexpr const char* kRxPin = "rx_pin";
constexpr const char* kTxPin = "tx_pin";
constexpr const char* kBaud = "baud";
}  // namespace gnss

namespace wifi {
constexpr const char* kNamespace = "wifi";
constexpr const char* kSsid = "ssid";
constexpr const char* kPass = "pass";
constexpr const char* kDhcp = "dhcp";
constexpr const char* kIp = "ip";
constexpr const char* kGw = "gw";
constexpr const char* kSubnet = "subnet";
constexpr const char* kDns = "dns";
constexpr const char* kAccessPoint = "accesspoint";
}  // namespace wifi

namespace ntrip {
constexpr const char* kNamespace = "ntrip";
constexpr const char* kEnabled = "enabled";
constexpr const char* kHost = "host";
constexpr const char* kPort = "port";
constexpr const char* kMount = "mount";
constexpr const char* kUser = "user";
constexpr const char* kPass = "pass";
constexpr const char* kMaxTries = "max_tries";
constexpr const char* kRetryDelay = "retry_delay";
constexpr const char* kHealthTimeout = "health_to";
constexpr const char* kPassiveMs = "passive_ms";
constexpr const char* kReqValid = "req_valid";
constexpr const char* kBufferSize = "buf_size";
constexpr const char* kConnectTimeout = "conn_to";

namespace lockout {
constexpr const char* kAttempts = "lock_fails";
constexpr const char* kAbandoned = "lock_aband";
constexpr const char* kHash = "lock_hash";
}  // namespace lockout
}  // namespace ntrip

}  // namespace nvs_keys
