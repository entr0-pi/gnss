// ======================= web_ui.cpp =======================

#include "app.h"
#define MODULE_LOG 1
#include "logger.h"

#if WEBUI_ENABLE

#include <WiFi.h>
#include <WiFiClient.h>
#include <LittleFS.h>
#include <cstring>
#include <ArduinoJson.h>

#include "config_gnss.h"
#include "config_ntrip.h"
#include "config_wifi.h"
#include "web_ui.h"
#include "internet_probe.h"

static WebServer* s_server = nullptr;
static IPAddress s_sta_dns;

static uint32_t g_http_req_total   = 0;
static uint32_t g_http_last_req_ms = 0;

static const char* const kPassMask = "********";

bool webui_get_internet_reachable() {
  return internet_probe_is_reachable();
}

static uint32_t markRequestAndGetPrevAgeMs() {
  const uint32_t now = millis();
  uint32_t age = 0;
  if (g_http_last_req_ms != 0) age = now - g_http_last_req_ms;
  g_http_last_req_ms = now;
  g_http_req_total++;
  return age;
}

// ---------------- Helpers ----------------

// Serialize a JsonDocument and send it as an HTTP JSON response.
static void sendJson(int code, JsonDocument& doc) {
  String output;
  doc.shrinkToFit();
  serializeJson(doc, output);
  s_server->sendHeader("Cache-Control", "no-store");
  s_server->send(code, "application/json", output);
}

static void sendJsonError(int code, const char* error) {
  JsonDocument doc;
  doc["ok"]    = false;
  doc["error"] = error;
  sendJson(code, doc);
}

// Stream a static asset from LittleFS (always gzip).
static void sendFileFromLittleFs(const char* path,
                                 const char* contentType,
                                 const char* cacheControl) {
  LOG_D("WEBUI", "Request file: %s", path);
  String gzPath = String(path) + ".gz";

  File file = LittleFS.open(gzPath, "r");
  if (!file) {
    LOG_E("WEBUI", "File not found: %s", gzPath.c_str());
    s_server->send(404, "text/plain", "404 Not Found");
    return;
  }

  s_server->sendHeader("Cache-Control", cacheControl);
  s_server->sendHeader("Content-Encoding", "gzip");
  s_server->sendHeader("Vary", "Accept-Encoding");
  s_server->setContentLength(file.size());
  s_server->send(200, contentType, "");

  WiFiClient client = s_server->client();
  uint8_t buffer[1024];
  while (file.available()) {
    const size_t n = file.read(buffer, sizeof(buffer));
    if (n == 0) break;
    client.write(buffer, n);
  }
  file.close();
}

// ------------- API: /api/status -------------

static void handleStatus() {
  const uint32_t prev_age_ms = markRequestAndGetPrevAgeMs();
  const uint32_t now = millis();

  const bool internet = internet_probe_is_reachable();

  WebuiBleSnapshot ble{};
  const bool has_ble = webui_get_ble_snapshot(ble);

  #if NMEA_ENABLE
  WebuiGpsSnapshot gps{};
  const bool has_gps = webui_get_gps_snapshot(gps);
  #endif

  #if NTRIP_CLIENT_ENABLE
  WebuiNtripSnapshot ntrip{};
  const bool has_ntrip = webui_get_ntrip_snapshot(ntrip);
  #endif

  JsonDocument doc;

  // device
  {
    JsonObject device = doc["device"].to<JsonObject>();
    device["uptime_ms"] = now;
    device["cpu_mhz"]   = ESP.getCpuFreqMHz();
    char build[32];
    snprintf(build, sizeof(build), "%s %s", __DATE__, __TIME__);
    device["build"] = build;
  }

  // memory
  {
    JsonObject mem = doc["memory"].to<JsonObject>();
    mem["heap_free"]      = ESP.getFreeHeap();
    mem["heap_min_free"]  = ESP.getMinFreeHeap();
    mem["heap_max_alloc"] = ESP.getMaxAllocHeap();
  }

  // wifi
  {
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["ssid"]      = WiFi.SSID();
    wifi["ip"]        = WiFi.localIP().toString();
    wifi["gw"]        = WiFi.gatewayIP().toString();
    wifi["dns"]       = s_sta_dns.toString();
    wifi["subnet"]    = WiFi.subnetMask().toString();
    wifi["broadcast"] = WiFi.broadcastIP().toString();
    wifi["rssi_dbm"]  = WiFi.RSSI();
    wifi["mac"]       = WiFi.macAddress();
  }

  // http
  {
    JsonObject http = doc["http"].to<JsonObject>();
    http["port"]            = 80;
    http["req_total"]       = g_http_req_total;
    http["prev_req_age_ms"] = prev_age_ms;
  }

  // app
  {
    JsonObject app = doc["app"].to<JsonObject>();
    app["state"] = "idle";
    app["notes"] = "ready";
  }

  // ble
  if (has_ble) {
    JsonObject bleObj = doc["ble"].to<JsonObject>();
    bleObj["connected"]      = ble.connected;
    bleObj["mtu"]            = ble.mtu;
    bleObj["txBytes"]        = ble.txBytes;
    bleObj["rxBytes"]        = ble.rxBytes;
    bleObj["uart2bleDrops"]  = ble.uart2bleDrops;
    bleObj["ble2uartDrops"]  = ble.ble2uartDrops;
  }

  // tcp
  #if TCP_ENABLE
  {
    WebuiTcpSnapshot tcp{};
    if (webui_get_tcp_snapshot(tcp)) {
      JsonObject tcpObj = doc["tcp"].to<JsonObject>();
      tcpObj["connected"]      = tcp.connected;
      tcpObj["txBytes"]        = tcp.txBytes;
      tcpObj["rxBytes"]        = tcp.rxBytes;
      tcpObj["uart2tcpDrops"]  = tcp.uart2tcpDrops;
      tcpObj["tcp2uartDrops"]  = tcp.tcp2uartDrops;
    }
  }
  #endif

  // gps
  #if NMEA_ENABLE
  if (has_gps) {
    const char* fixTypeStr = "—";
    if      (gps.fixType == 1) fixTypeStr = "No";
    else if (gps.fixType == 2) fixTypeStr = "2D";
    else if (gps.fixType == 3) fixTypeStr = "3D";

    const char* fixQualStr = "—";
    switch (gps.fixQuality) {
      case 0: fixQualStr = "Invalid"; break;
      case 1: fixQualStr = "GNSS";    break;
      case 2: fixQualStr = "DGPS";    break;
      case 4: fixQualStr = "RTK Fix"; break;
      case 5: fixQualStr = "RTK Flt"; break;
      default: fixQualStr = "Other";  break;
    }

    const char* accSrcStr = "—";
    if      (gps.accSource == 1) accSrcStr = "GST";
    else if (gps.accSource == 2) accSrcStr = "HDOP-est";

    char tbuf[16];
    if (gps.timeValid) snprintf(tbuf, sizeof(tbuf), "%02u:%02u:%02u", gps.hour, gps.minute, gps.second);
    else               snprintf(tbuf, sizeof(tbuf), "—");

    char dbuf[16];
    if (gps.year && gps.month && gps.day) snprintf(dbuf, sizeof(dbuf), "%04u-%02u-%02u", gps.year, gps.month, gps.day);
    else                                  snprintf(dbuf, sizeof(dbuf), "—");

    JsonObject gpsObj = doc["gps"].to<JsonObject>();
    gpsObj["valid"]              = gps.valid || (gps.fixQuality == 4) || (gps.fixQuality == 5);
    gpsObj["age_ms"]             = gps.ageMs;
    gpsObj["lat"]                = gps.lat;
    gpsObj["lon"]                = gps.lon;
    gpsObj["speed_kmh"]          = gps.speedKmh;
    gpsObj["sats_used"]          = gps.satsUsed;
    gpsObj["hdop"]               = gps.hdop;
    gpsObj["hacc_m"]             = gps.hAcc_m;
    gpsObj["vacc_m"]             = gps.vAcc_m;
    gpsObj["acc_source"]         = accSrcStr;
    gpsObj["acc_source_code"]    = gps.accSource;
    gpsObj["fix_type"]           = fixTypeStr;
    gpsObj["fix_quality"]        = fixQualStr;
    gpsObj["fix_type_code"]      = gps.fixType;
    gpsObj["fix_quality_code"]   = gps.fixQuality;
    gpsObj["time_utc"]           = tbuf;
    gpsObj["date_utc"]           = dbuf;
    gpsObj["satellites_in_view"] = gps.satCount;

    if (gps.satCount > 0) {
      static const char* const consNames[] = {
        "GPS", "GLONASS", "Galileo", "BeiDou", "Other"
      };
      JsonArray satsArr = gpsObj["satellites"].to<JsonArray>();
      for (uint8_t i = 0; i < gps.satCount; i++) {
        JsonObject sat = satsArr.add<JsonObject>();
        sat["constellation"] = consNames[
            (gps.sats[i].constellation <= 4) ? gps.sats[i].constellation : 4];
        sat["nr"]           = gps.sats[i].nr;
        sat["elevation"]    = gps.sats[i].elevation;
        sat["azimuth"]      = gps.sats[i].azimuth;
        sat["signal_power"] = gps.sats[i].snr;
      }
    }
  }
  #endif

  // ntrip
  #if NTRIP_CLIENT_ENABLE
  if (has_ntrip) {
    JsonObject ntripObj = doc["ntrip"].to<JsonObject>();
    ntripObj["connected"]         = ntrip.connected;
    ntripObj["healthy"]           = ntrip.healthy;
    ntripObj["streaming"]         = ntrip.streaming;
    ntripObj["bytes_received"]    = ntrip.bytesReceived;
    ntripObj["total_frames"]      = ntrip.totalFrames;
    ntripObj["last_msg_type"]     = ntrip.lastMessageType;
    ntripObj["last_frame_age_ms"] = ntrip.lastFrameAgeMs;
    ntripObj["protocol_version"]  = ntrip.protocolVersion;
  }
  #endif

  // internet
  {
    JsonObject net = doc["internet"].to<JsonObject>();
    net["reach"] = internet;
  }

  sendJson(200, doc);
}

// ------------- API: /api/restart -------------

static void handleRestart() {
  markRequestAndGetPrevAgeMs();
  s_server->sendHeader("Cache-Control", "no-store");
  s_server->send(204, "text/plain", "");
  delay(150);
  ESP.restart();
}

// ------------- API: /api/config -------------

static void handleUARTConfigGet() {
  markRequestAndGetPrevAgeMs();

  GnssConfig cfg = gnss_config_defaults();
  gnss_config_load(cfg, nullptr);
  const GnssConfig defaults = gnss_config_defaults();

  JsonDocument doc;
  doc["rx_pin"] = cfg.rx_pin;
  doc["tx_pin"] = cfg.tx_pin;
  doc["baud"]   = cfg.baud;
  doc["locked"] = (bool)IMMUTABLE_UART;

  JsonObject defObj = doc["defaults"].to<JsonObject>();
  defObj["rx_pin"] = defaults.rx_pin;
  defObj["tx_pin"] = defaults.tx_pin;
  defObj["baud"]   = defaults.baud;

  sendJson(200, doc);
}

static void handleUARTConfigPost() {
  markRequestAndGetPrevAgeMs();

  #if IMMUTABLE_UART
    sendJsonError(403, "UART config is locked via build flags");
    return;
  #endif

  if (!s_server->hasArg("plain")) {
    sendJsonError(400, "Missing body");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, s_server->arg("plain"))) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  if (!doc["rx_pin"].is<int>() || !doc["tx_pin"].is<int>() || !doc["baud"].is<uint32_t>()) {
    sendJsonError(400, "rx_pin, tx_pin, and baud are required");
    return;
  }

  GnssConfig cfg{};
  cfg.rx_pin = doc["rx_pin"].as<int>();
  cfg.tx_pin = doc["tx_pin"].as<int>();
  cfg.baud   = doc["baud"].as<uint32_t>();

  String error;
  if (!gnss_apply_runtime_config(cfg, &error)) {
    sendJsonError(400, error.c_str());
    return;
  }

  JsonDocument resp;
  resp["ok"]      = true;
  resp["message"] = "Config saved";
  JsonObject cfgObj = resp["config"].to<JsonObject>();
  cfgObj["rx_pin"] = cfg.rx_pin;
  cfgObj["tx_pin"] = cfg.tx_pin;
  cfgObj["baud"]   = cfg.baud;

  sendJson(200, resp);
}

// ------------- API: /api/wifi_config -------------

static void handleWifiConfigGet() {
  markRequestAndGetPrevAgeMs();

  WifiConfig cfg{};
  String error;
  const bool loaded = wifi_config_load(cfg, &error);

  JsonDocument doc;
  doc["loaded"] = loaded;
  if (!loaded) doc["error"] = error;

  doc["locked"] = (bool)IMMUTABLE_WIFI;

  if (!loaded) {
    cfg.ssid   = STA_SSID;
    cfg.pass   = STA_PASS;
    cfg.dhcp   = false;
    cfg.accesspoint = true;
    cfg.ip     = STA_IP;
    cfg.gw     = STA_GW;
    cfg.subnet = STA_SUBNET;
    cfg.dns    = STA_DNS;
  }

  doc["ssid"]   = cfg.ssid;
  doc["pass"]   = kPassMask;
  doc["dhcp"]   = cfg.dhcp;
  doc["accesspoint"] = cfg.accesspoint;
  doc["dual_mode_supported"] = (bool)WIFI_DUAL_MODE;
  doc["ip"]     = cfg.ip.toString();
  doc["gw"]     = cfg.gw.toString();
  doc["subnet"] = cfg.subnet.toString();
  doc["dns"]    = cfg.dns.toString();

  sendJson(200, doc);
}

static void handleWifiConfigPost() {
  markRequestAndGetPrevAgeMs();

  #if IMMUTABLE_WIFI
    sendJsonError(403, "WiFi config is locked via build flags");
    return;
  #endif

  if (!s_server->hasArg("plain")) {
    sendJsonError(400, "Missing body");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, s_server->arg("plain"))) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  if (!doc["ssid"].is<const char*>() || !doc["pass"].is<const char*>() || !doc["dhcp"].is<bool>() || !doc["accesspoint"].is<bool>()) {
    sendJsonError(400, "ssid, pass, dhcp, and accesspoint are required");
    return;
  }

  WifiConfig cfg{};
  cfg.ssid = doc["ssid"].as<String>();
  cfg.pass = doc["pass"].as<String>();
  cfg.dhcp = doc["dhcp"].as<bool>();
  cfg.accesspoint = doc["accesspoint"].as<bool>();

  // If the password was not changed (masked sentinel), preserve existing.
  if (cfg.pass == kPassMask) {
    WifiConfig existing{};
    String loadErr;
    if (wifi_config_load(existing, &loadErr)) {
      cfg.pass = existing.pass;
    }
    // else: keeps mask value — user should enter a real password on first setup
  }

  if (cfg.ssid.isEmpty()) {
    sendJsonError(400, "ssid is empty");
    return;
  }

  if (!cfg.dhcp) {
    if (!doc["ip"].is<const char*>() || !doc["gw"].is<const char*>() ||
        !doc["subnet"].is<const char*>() || !doc["dns"].is<const char*>()) {
      sendJsonError(400, "ip, gw, subnet, and dns are required when dhcp is false");
      return;
    }

    if (!cfg.ip.fromString(doc["ip"].as<String>()) ||
        !cfg.gw.fromString(doc["gw"].as<String>()) ||
        !cfg.subnet.fromString(doc["subnet"].as<String>()) ||
        !cfg.dns.fromString(doc["dns"].as<String>())) {
      sendJsonError(400, "Invalid IP fields");
      return;
    }
  } else {
    cfg.ip     = IPAddress(0, 0, 0, 0);
    cfg.gw     = IPAddress(0, 0, 0, 0);
    cfg.subnet = IPAddress(0, 0, 0, 0);
    cfg.dns    = IPAddress(0, 0, 0, 0);
  }

  String error;
  if (!wifi_config_save(cfg, &error)) {
    sendJsonError(400, error.c_str());
    return;
  }

  JsonDocument resp;
  resp["ok"]      = true;
  resp["message"] = "WiFi config saved";
  JsonObject cfgObj = resp["config"].to<JsonObject>();
  cfgObj["ssid"]   = cfg.ssid;
  cfgObj["pass"]   = kPassMask;
  cfgObj["dhcp"]   = cfg.dhcp;
  cfgObj["accesspoint"] = cfg.accesspoint;
  cfgObj["ip"]     = cfg.ip.toString();
  cfgObj["gw"]     = cfg.gw.toString();
  cfgObj["subnet"] = cfg.subnet.toString();
  cfgObj["dns"]    = cfg.dns.toString();

  sendJson(200, resp);
}

// ------------- API: /api/ntrip_config -------------

static void handleNtripConfigGet() {
  markRequestAndGetPrevAgeMs();

  NtripConfig cfg{};
  NtripLockout lockout{};
  String error;
  const bool loaded = ntrip_config_load(cfg, &lockout, &error);

  JsonDocument doc;
  doc["locked"] = (bool)IMMUTABLE_NTRIP;

  if (loaded) {
    JsonObject ntrip = doc["ntrip"].to<JsonObject>();
    ntrip["enabled"]               = cfg.enabled;
    ntrip["host"]                  = cfg.host;
    ntrip["port"]                  = cfg.port;
    ntrip["mount"]                 = cfg.mount;
    ntrip["user"]                  = cfg.user;
    ntrip["pass"]                  = cfg.pass;
    ntrip["max_tries"]             = cfg.max_tries;
    ntrip["retry_delay_ms"]        = cfg.retry_delay_ms;
    ntrip["health_timeout_ms"]     = cfg.health_timeout_ms;
    ntrip["passive_sample_ms"]     = cfg.passive_sample_ms;
    ntrip["required_valid_frames"] = cfg.required_valid_frames;
    ntrip["buffer_size"]           = cfg.buffer_size;
    ntrip["connect_timeout_ms"]    = cfg.connect_timeout_ms;
    ntrip["send_gga"]              = cfg.send_gga;

    JsonObject lockObj = doc["lockout"].to<JsonObject>();
    lockObj["failed_attempts"]  = lockout.failed_attempts;
    lockObj["abandoned"]        = lockout.abandoned;
    lockObj["last_config_hash"] = lockout.last_config_hash;
  } else {
    doc["error"] = error;
  }

  sendJson(200, doc);
}

static void handleNtripConfigPost() {
  markRequestAndGetPrevAgeMs();

  #if IMMUTABLE_NTRIP
    sendJsonError(403, "NTRIP config is locked");
    return;
  #endif

  if (!s_server->hasArg("plain")) {
    sendJsonError(400, "Missing body");
    return;
  }

  JsonDocument input;
  if (deserializeJson(input, s_server->arg("plain"))) {
    sendJsonError(400, "Invalid JSON");
    return;
  }

  if (!input["ntrip"].is<JsonObject>()) {
    sendJsonError(400, "ntrip object is required");
    return;
  }

  JsonObject ntripIn = input["ntrip"].as<JsonObject>();
  if (!ntripIn["enabled"].is<bool>() ||
      !ntripIn["host"].is<const char*>() ||
      !ntripIn["port"].is<uint16_t>() ||
      !ntripIn["mount"].is<const char*>() ||
      !ntripIn["user"].is<const char*>() ||
      !ntripIn["pass"].is<const char*>() ||
      !ntripIn["max_tries"].is<int>() ||
      !ntripIn["retry_delay_ms"].is<uint32_t>() ||
      !ntripIn["health_timeout_ms"].is<uint32_t>() ||
      !ntripIn["passive_sample_ms"].is<uint32_t>() ||
      !ntripIn["required_valid_frames"].is<uint32_t>() ||
      !ntripIn["buffer_size"].is<uint32_t>() ||
      !ntripIn["connect_timeout_ms"].is<uint32_t>() ||
      !ntripIn["send_gga"].is<bool>()) {
    sendJsonError(400, "Invalid or missing NTRIP fields");
    return;
  }

  NtripConfig cfg{};
  cfg.enabled              = ntripIn["enabled"].as<bool>();
  cfg.host                 = ntripIn["host"].as<String>();
  cfg.port                 = ntripIn["port"].as<uint16_t>();
  cfg.mount                = ntripIn["mount"].as<String>();
  cfg.user                 = ntripIn["user"].as<String>();
  cfg.pass                 = ntripIn["pass"].as<String>();
  cfg.max_tries            = ntripIn["max_tries"].as<int>();
  cfg.retry_delay_ms       = ntripIn["retry_delay_ms"].as<uint32_t>();
  cfg.health_timeout_ms    = ntripIn["health_timeout_ms"].as<uint32_t>();
  cfg.passive_sample_ms    = ntripIn["passive_sample_ms"].as<uint32_t>();
  cfg.required_valid_frames = ntripIn["required_valid_frames"].as<uint32_t>();
  cfg.buffer_size          = ntripIn["buffer_size"].as<uint32_t>();
  cfg.connect_timeout_ms   = ntripIn["connect_timeout_ms"].as<uint32_t>();
  cfg.send_gga             = ntripIn["send_gga"].as<bool>();

  String valError;
  if (!ntrip_config_validate(cfg, &valError)) {
    sendJsonError(400, valError.c_str());
    return;
  }

  // Preserve existing lockout state across config saves.
  NtripLockout lockout{};
  NtripConfig existing{};
  ntrip_config_load(existing, &lockout, nullptr);

  String saveError;
  if (!ntrip_config_save(cfg, &lockout, &saveError)) {
    sendJsonError(500, saveError.c_str());
    return;
  }

  JsonDocument resp;
  resp["ok"]      = true;
  resp["message"] = "NTRIP config saved";
  JsonObject cfgObj = resp["config"].to<JsonObject>();
  cfgObj["enabled"]               = cfg.enabled;
  cfgObj["host"]                  = cfg.host;
  cfgObj["port"]                  = cfg.port;
  cfgObj["mount"]                 = cfg.mount;
  cfgObj["user"]                  = cfg.user;
  cfgObj["pass"]                  = cfg.pass;
  cfgObj["max_tries"]             = cfg.max_tries;
  cfgObj["retry_delay_ms"]        = cfg.retry_delay_ms;
  cfgObj["health_timeout_ms"]     = cfg.health_timeout_ms;
  cfgObj["passive_sample_ms"]     = cfg.passive_sample_ms;
  cfgObj["required_valid_frames"] = cfg.required_valid_frames;
  cfgObj["buffer_size"]           = cfg.buffer_size;
  cfgObj["connect_timeout_ms"]    = cfg.connect_timeout_ms;
  cfgObj["send_gga"]              = cfg.send_gga;

  sendJson(200, resp);
}

// ------------- Route registration -------------

void webui_begin(WebServer& server, const IPAddress& sta_dns) {
  s_server  = &server;
  s_sta_dns = sta_dns;

  if (!LittleFS.begin()) {
    LOG_E("WEBUI", "LittleFS mount failed");
  } else {
    LOG_I("WEBUI", "LittleFS mounted");
    LOG_D("WEBUI", "index.html.gz: %s", LittleFS.exists("/web/index.html.gz") ? "yes" : "no");
    LOG_D("WEBUI", "app.js.gz: %s",     LittleFS.exists("/web/app.js.gz") ? "yes" : "no");
    LOG_D("WEBUI", "style.css.gz: %s",   LittleFS.exists("/web/style.css.gz") ? "yes" : "no");
  }

  server.on("/", HTTP_GET, []() {
    markRequestAndGetPrevAgeMs();
    sendFileFromLittleFs("/web/index.html", "text/html; charset=utf-8", "no-store");
  });

  server.on("/style.css", HTTP_GET, []() {
    markRequestAndGetPrevAgeMs();
    sendFileFromLittleFs("/web/style.css", "text/css; charset=utf-8", "no-store");
  });

  server.on("/app.js", HTTP_GET, []() {
    markRequestAndGetPrevAgeMs();
    sendFileFromLittleFs("/web/app.js", "application/javascript; charset=utf-8", "no-store");
  });

  server.on("/favicon.ico", HTTP_GET, []() {
    markRequestAndGetPrevAgeMs();
    sendFileFromLittleFs("/web/favicon.ico", "image/x-icon", "public, max-age=604800");
  });

  server.on("/api/status",       HTTP_GET,  handleStatus);
  server.on("/api/config",       HTTP_GET,  handleUARTConfigGet);
  server.on("/api/config",       HTTP_POST, handleUARTConfigPost);
  server.on("/api/wifi_config",  HTTP_GET,  handleWifiConfigGet);
  server.on("/api/wifi_config",  HTTP_POST, handleWifiConfigPost);
  server.on("/api/ntrip_config", HTTP_GET,  handleNtripConfigGet);
  server.on("/api/ntrip_config", HTTP_POST, handleNtripConfigPost);
  server.on("/api/restart",      HTTP_POST, handleRestart);

  LOG_I("WEBUI", "Routes registered");

  server.onNotFound([]() {
    markRequestAndGetPrevAgeMs();
    s_server->send(404, "text/plain", "404 Not Found");
  });
}

#else

#include "web_ui.h"
#include "internet_probe.h"

void webui_begin(WebServer& server, const IPAddress& sta_dns) {
  (void)server;
  (void)sta_dns;
}

#endif
