#include "wifi_config.h"

#include <Preferences.h>
#include "nvs_keys.h"

namespace {
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
  if (!prefs.begin(nvs_keys::wifi::kNamespace, true)) {
    if (error) {
      *error = "NVS open failed";
    }
    return false;
  }

  const bool has_ssid = prefs.isKey(nvs_keys::wifi::kSsid);
  const bool has_pass = prefs.isKey(nvs_keys::wifi::kPass);
  const bool has_dhcp = prefs.isKey(nvs_keys::wifi::kDhcp);
  if (!has_ssid || !has_pass || !has_dhcp) {
    prefs.end();
    if (error) {
      *error = "WiFi config not found in NVS";
    }
    return false;
  }

  cfg.ssid = prefs.getString(nvs_keys::wifi::kSsid);
  cfg.pass = prefs.getString(nvs_keys::wifi::kPass);
  cfg.dhcp = prefs.getBool(nvs_keys::wifi::kDhcp);

  if (!cfg.dhcp) {
    const String ip_str = prefs.getString(nvs_keys::wifi::kIp);
    const String gw_str = prefs.getString(nvs_keys::wifi::kGw);
    const String subnet_str = prefs.getString(nvs_keys::wifi::kSubnet);
    const String dns_str = prefs.getString(nvs_keys::wifi::kDns);
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
  if (!prefs.begin(nvs_keys::wifi::kNamespace, false)) {
    if (error) {
      *error = "NVS open failed";
    }
    return false;
  }

  bool ok = true;
  ok = ok && prefs.putString(nvs_keys::wifi::kSsid, cfg.ssid) > 0;
  ok = ok && prefs.putString(nvs_keys::wifi::kPass, cfg.pass) > 0;
  ok = ok && prefs.putBool(nvs_keys::wifi::kDhcp, cfg.dhcp);

  if (cfg.dhcp) {
    ok = ok && prefs.putString(nvs_keys::wifi::kIp, "0.0.0.0") > 0;
    ok = ok && prefs.putString(nvs_keys::wifi::kGw, "0.0.0.0") > 0;
    ok = ok && prefs.putString(nvs_keys::wifi::kSubnet, "0.0.0.0") > 0;
    ok = ok && prefs.putString(nvs_keys::wifi::kDns, "0.0.0.0") > 0;
  } else {
    ok = ok && prefs.putString(nvs_keys::wifi::kIp, cfg.ip.toString()) > 0;
    ok = ok && prefs.putString(nvs_keys::wifi::kGw, cfg.gw.toString()) > 0;
    ok = ok && prefs.putString(nvs_keys::wifi::kSubnet, cfg.subnet.toString()) > 0;
    ok = ok && prefs.putString(nvs_keys::wifi::kDns, cfg.dns.toString()) > 0;
  }

  prefs.end();

  if (!ok) {
    if (error) *error = "WiFi config write failed";
    return false;
  }

  return true;
}
