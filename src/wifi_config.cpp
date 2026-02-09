#include "wifi_config.h"

#include <Preferences.h>

namespace {
constexpr const char* kWifiNvsNs = "wifi";

bool parse_ip_field(const String& value, const char* field, IPAddress& out, String* error) {
  if (value.isEmpty()) {
    if (error) {
      *error = String("WiFi config missing or invalid ") + field;
    }
    return false;
  }

  if (!out.fromString(value)) {
    if (error) {
      *error = String("WiFi config ") + field + " is not a valid IP: " + value;
    }
    return false;
  }

  return true;
}
} // namespace

bool wifi_config_load(WifiConfig& cfg, String* error) {
  Preferences prefs;
  if (!prefs.begin(kWifiNvsNs, true)) {
    if (error) {
      *error = "NVS open failed";
    }
    return false;
  }

  const bool has_ssid = prefs.isKey("ssid");
  const bool has_pass = prefs.isKey("pass");
  const bool has_dhcp = prefs.isKey("dhcp");
  if (!has_ssid || !has_pass || !has_dhcp) {
    prefs.end();
    if (error) {
      *error = "WiFi config not found in NVS";
    }
    return false;
  }

  cfg.ssid = prefs.getString("ssid", "");
  cfg.pass = prefs.getString("pass", "");
  cfg.dhcp = prefs.getBool("dhcp", false);

  if (!cfg.dhcp) {
    const String ip_str = prefs.getString("ip", "");
    const String gw_str = prefs.getString("gw", "");
    const String subnet_str = prefs.getString("subnet", "");
    const String dns_str = prefs.getString("dns", "");
    prefs.end();

    if (!parse_ip_field(ip_str, "ip", cfg.ip, error)) return false;
    if (!parse_ip_field(gw_str, "gw", cfg.gw, error)) return false;
    if (!parse_ip_field(subnet_str, "subnet", cfg.subnet, error)) return false;
    if (!parse_ip_field(dns_str, "dns", cfg.dns, error)) return false;
  } else {
    prefs.end();
    cfg.ip = IPAddress(0, 0, 0, 0);
    cfg.gw = IPAddress(0, 0, 0, 0);
    cfg.subnet = IPAddress(0, 0, 0, 0);
    cfg.dns = IPAddress(0, 0, 0, 0);
  }

  if (cfg.ssid.isEmpty()) {
    if (error) {
      *error = "WiFi config ssid is empty";
    }
    return false;
  }

  return true;
}

bool wifi_config_save(const WifiConfig& cfg, String* error) {
  if (cfg.ssid.isEmpty()) {
    if (error) *error = "WiFi config ssid is empty";
    return false;
  }

  if (!cfg.dhcp) {
    if (cfg.ip == IPAddress(0, 0, 0, 0) || cfg.gw == IPAddress(0, 0, 0, 0) ||
        cfg.subnet == IPAddress(0, 0, 0, 0) || cfg.dns == IPAddress(0, 0, 0, 0)) {
      if (error) *error = "WiFi config requires ip, gw, subnet, and dns when dhcp is false";
      return false;
    }
  }

  Preferences prefs;
  if (!prefs.begin(kWifiNvsNs, false)) {
    if (error) {
      *error = "NVS open failed";
    }
    return false;
  }

  bool ok = true;
  ok = ok && prefs.putString("ssid", cfg.ssid) > 0;
  ok = ok && prefs.putString("pass", cfg.pass) > 0;
  ok = ok && prefs.putBool("dhcp", cfg.dhcp);

  if (cfg.dhcp) {
    ok = ok && prefs.putString("ip", "0.0.0.0") > 0;
    ok = ok && prefs.putString("gw", "0.0.0.0") > 0;
    ok = ok && prefs.putString("subnet", "0.0.0.0") > 0;
    ok = ok && prefs.putString("dns", "0.0.0.0") > 0;
  } else {
    ok = ok && prefs.putString("ip", cfg.ip.toString()) > 0;
    ok = ok && prefs.putString("gw", cfg.gw.toString()) > 0;
    ok = ok && prefs.putString("subnet", cfg.subnet.toString()) > 0;
    ok = ok && prefs.putString("dns", cfg.dns.toString()) > 0;
  }

  prefs.end();

  if (!ok) {
    if (error) *error = "WiFi config write failed";
    return false;
  }

  return true;
}
