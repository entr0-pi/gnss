// ======================= web_ui.cpp (FINAL - JSON built here) =======================

#include "app.h"

#if WEBUI_ENABLE
//
// This module owns the HTTP UI endpoints and the JSON status API.
//
// Responsibilities:
// - Serve static assets stored in PROGMEM (HTML/CSS/favicon)
// - Expose JSON status at /api/status
// - Provide a restart endpoint at /api/restart
// - Optionally probe “internet reachable” (HTTP 204 connectivity check)
// - Maintain simple HTTP request stats (total count + time since previous request)
//
// Design notes:
// - This uses Arduino WebServer (synchronous, polled via server.handleClient()).
// - JSON is built here using ArduinoJson (v7 in your comment).
// - BLE/GPS info are pulled via snapshot getter functions defined elsewhere (main.cpp / nmea module).
//
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <cstring>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "app_js.h"
#include "app_index.h"
#include "app_style.h"
#include "app_favicon.h"
#include "gnss_config.h"
#include "wifi_config.h"
#include "web_ui.h"

// Keep a pointer so helpers/handlers can use the same server instance.
// We store a pointer rather than a reference so we can set it in webui_begin().
static WebServer* s_server = nullptr;

// Keep DNS for status.
// WiFi.dnsIP() isn't always exposed the way you want in Arduino, so you pass it in from main.
static IPAddress s_sta_dns;

// ---------------- Simple HTTP stats ----------------
//
// g_http_req_total:
//   Count of HTTP requests served (for your dashboard).
// g_http_last_req_ms:
//   Timestamp (millis) of the most recent request.
static uint32_t g_http_req_total = 0;
static uint32_t g_http_last_req_ms = 0;

// Last known internet reachability from /api/status probes.
static bool g_internet_reachable = false;

bool webui_get_internet_reachable() {
  return g_internet_reachable;
}

#if NTRIP_CLIENT_ENABLE
static const char* kNtripConfigPath = "/ntrip_config.json";
#endif

// markRequestAndGetPrevAgeMs():
// - Updates request counters
// - Computes "age since previous request" in ms
// - Returns that age so it can be included in JSON
static uint32_t markRequestAndGetPrevAgeMs() {
  const uint32_t now = millis();
  uint32_t age = 0;

  // If there was a previous request, compute time since then.
  if (g_http_last_req_ms != 0) age = now - g_http_last_req_ms;

  // Update last request timestamp and total counter.
  g_http_last_req_ms = now;
  g_http_req_total++;

  return age;
}

// sendProgmem():
// Serve a PROGMEM buffer (text or binary) using WebServer::send_P().
// Parameters:
// - code:         HTTP status code (200, 404, etc.)
// - contentType:  MIME type (e.g., text/html, text/css, image/x-icon)
// - data/len:     PROGMEM buffer and length
// - cacheControl: Cache-Control header (e.g., "no-store" for HTML/JSON, long max-age for static assets)
static void sendProgmem(int code,
                        const char* contentType,
                        const uint8_t* data,
                        size_t len,
                        const char* cacheControl) {
  // Defensive: s_server should have been set by webui_begin().
  // (Your code assumes it's valid; we keep behavior identical.)
  s_server->sendHeader("Cache-Control", cacheControl);

  // send_P reads from PROGMEM; cast to char* is required by API signature.
  s_server->send_P(code, contentType, (const char*)data, len);
}

// sendProgmemGzip():
// Serve a gzipped PROGMEM buffer and set Content-Encoding: gzip.
static void sendProgmemGzip(int code,
                            const char* contentType,
                            const uint8_t* data,
                            size_t len,
                            const char* cacheControl) {
  s_server->sendHeader("Content-Encoding", "gzip");
  sendProgmem(code, contentType, data, len, cacheControl);
}

// ------------- API: internet reachable -------------
//
// logInternetHTTP():
// Performs a simple "connectivity check" HTTP GET.
// - Uses Google's generate_204 endpoint which returns HTTP 204 if reachable.
// - Prints diagnostic messages to Serial.
// - Returns true if we got HTTP 204, false otherwise.
//
// Important:
// - This is a synchronous/blocking check and will add latency to /api/status.
// - Timeout is set to 2.5 seconds.
static bool logInternetHTTP() {
  // If STA is not connected, no point trying HTTP.
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[NET] WiFi not connected");
    return false;
  }

  // WiFiClient provides the underlying TCP socket.
  WiFiClient client;

  // HTTPClient is the Arduino high-level HTTP wrapper.
  HTTPClient http;

  // Keep this short so /api/status doesn't block too long.
  http.setTimeout(2500);

  // Avoid keep-alive reuse (simplifies behavior on embedded).
  http.setReuse(false);

  // Endpoint commonly used for captive portal / connectivity checks.
  // Success case: HTTP 204 No Content.
  const char* url = "http://connectivitycheck.gstatic.com/generate_204";

  // Prepare HTTP request (DNS, TCP connect, etc.)
  if (!http.begin(client, url)) {
    Serial.println("[NET] http.begin() failed");
    return false;
  }

  // Perform the GET request. On error, code is negative.
  int code = http.GET();
  Serial.print("[NET] HTTP code: ");
  Serial.println(code);

  bool ok = false;

  // Interpret result.
  if (code == 204) {
    Serial.println("[NET] Internet reachable");
    ok = true;
  } else if (code > 0) {
    Serial.println("[NET] Reached server, but unexpected code");
    ok = false;
  } else {
    // Negative code means transport error (timeout, DNS fail, etc.)
    Serial.print("[NET] HTTP GET failed, err=");
    Serial.println(http.errorToString(code)); // code is negative on error
    ok = false;
  }

  // Always release resources (closes TCP, frees internal buffers).
  http.end();
  return ok;
}

// ------------- API: /api/status -------------
//
// handleStatus():
// - Collects current status from ESP/WiFi, plus optional BLE/GPS snapshots
// - Probes internet reachability (blocking)
// - Builds a JSON document and returns it to the browser
static void handleStatus() {
  // Track request stats and compute time since previous request.
  const uint32_t prev_age_ms = markRequestAndGetPrevAgeMs();
  const uint32_t now = millis();

  // --- Device ---
  // Uptime in ms is simply millis().
  const uint32_t uptime_ms = now;

  // CPU frequency in MHz (ESP32 API).
  const uint32_t cpu_mhz   = ESP.getCpuFreqMHz();

  // --- Memory ---
  // heap_free:      current free heap bytes
  // heap_min_free:  minimum ever free heap since boot (useful for fragmentation/peaks)
  // heap_max_alloc: maximum allocatable single block right now (fragmentation indicator)
  const uint32_t heap_free      = ESP.getFreeHeap();
  const uint32_t heap_min_free  = ESP.getMinFreeHeap();
  const uint32_t heap_max_alloc = ESP.getMaxAllocHeap();

  // --- WiFi (STA) ---
  // SSID can be empty if not connected.
  const String ssid  = WiFi.SSID();
  const String ip    = WiFi.localIP().toString();
  const String gw    = WiFi.gatewayIP().toString();
  const String dns   = s_sta_dns.toString();          // from main (stored in s_sta_dns)
  const String mask  = WiFi.subnetMask().toString();
  const String bcast = WiFi.broadcastIP().toString();
  const int32_t rssi = WiFi.RSSI();                   // RSSI in dBm (negative value)
  const String mac   = WiFi.macAddress();

  // --- Internet ---
  // You probe synchronously at each /api/status call.
  // HTML converts boolean -> ✅/❌ icons now.
  bool internet;
  if (logInternetHTTP()) {
    internet  = true;   // "✅" in html now
  }
  else {
    internet  = false;  // "❌" in html now
  }
  g_internet_reachable = internet;

  // --- HTTP ---
  // Static HTTP port used by this WebServer instance.
  const uint16_t http_port = 80;

  // --- App ---
  // Placeholders for application-level state.
  // (Could be upgraded later to reflect BLE/TCP/NTRIP etc.)
  const char* app_state = "idle";
  const char* app_notes = "ready";

  // --- BLE snapshot (optional) ---
  // Snapshot getter is implemented elsewhere (main.cpp).
  // has_ble indicates whether the snapshot was available.
  WebuiBleSnapshot ble{};
  const bool has_ble = webui_get_ble_snapshot(ble);

  // --- GPS snapshot (optional) ---
  #if NMEA_ENABLE
  WebuiGpsSnapshot gps{};
  const bool has_gps = webui_get_gps_snapshot(gps);
  #endif

  // --- JSON ---
  // ArduinoJson v7
  // JsonDocument is a dynamic document type (v7); it will allocate as needed.
  // (You later call shrinkToFit() to reduce memory after building.)
  JsonDocument doc;

  // ---------------- device ----------------
  {
    // Create nested object: doc["device"] = { ... }
    JsonObject device = doc["device"].to<JsonObject>();
    device["uptime_ms"] = uptime_ms;
    device["cpu_mhz"]   = cpu_mhz;

    // Build string once (compile-time constants __DATE__ and __TIME__).
    // Stored as a temporary char buffer copied into JsonDocument.
    char build[32];
    snprintf(build, sizeof(build), "%s %s", __DATE__, __TIME__);
    device["build"] = build;
  }

  // ---------------- memory ----------------
  {
    JsonObject mem = doc["memory"].to<JsonObject>();
    mem["heap_free"]      = heap_free;
    mem["heap_min_free"]  = heap_min_free;
    mem["heap_max_alloc"] = heap_max_alloc;
  }

  // ---------------- wifi ----------------
  {
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["ssid"]      = ssid;
    wifi["ip"]        = ip;
    wifi["gw"]        = gw;
    wifi["dns"]       = dns;
    wifi["subnet"]    = mask;
    wifi["broadcast"] = bcast;
    wifi["rssi_dbm"]  = rssi;
    wifi["mac"]       = mac;
  }

  // ---------------- http ----------------
  {
    JsonObject http = doc["http"].to<JsonObject>();
    http["port"]            = http_port;
    http["req_total"]       = g_http_req_total;
    http["prev_req_age_ms"] = prev_age_ms;
  }

  // ---------------- app ----------------
  {
    JsonObject app = doc["app"].to<JsonObject>();
    app["state"] = app_state;
    app["notes"] = app_notes;
  }

  // ---------------- ble (optional) ----------------
  // Only include if the snapshot getter succeeded.
  if (has_ble) {
    JsonObject bleObj = doc["ble"].to<JsonObject>();

    bleObj["connected"] = ble.connected; // Boolean;
    bleObj["mtu"]       = ble.mtu;
    bleObj["txBytes"]       = ble.txBytes;
    bleObj["rxBytes"]       = ble.rxBytes;
    bleObj["uart2bleDrops"] = ble.uart2bleDrops;
    bleObj["ble2uartDrops"] = ble.ble2uartDrops;
  }

  // ---------------- tcp (optional) ----------------
  #if TCP_ENABLE
  WebuiTcpSnapshot tcp{};
  const bool has_tcp = webui_get_tcp_snapshot(tcp);
  if (has_tcp) {
    JsonObject tcpObj = doc["tcp"].to<JsonObject>();
    tcpObj["connected"]    = tcp.connected;
    tcpObj["txBytes"]      = tcp.txBytes;
    tcpObj["rxBytes"]      = tcp.rxBytes;
    tcpObj["uart2tcpDrops"] = tcp.uart2tcpDrops;
    tcpObj["tcp2uartDrops"] = tcp.tcp2uartDrops;
  }
  #endif

  // ---------------- gps (optional) ----------------
  #if NMEA_ENABLE
  if (has_gps) {
    // Human-readable fix type string.
    const char* fixTypeStr = "—";
    if      (gps.fixType == 1) fixTypeStr = "No";
    else if (gps.fixType == 2) fixTypeStr = "2D";
    else if (gps.fixType == 3) fixTypeStr = "3D";

    // Human-readable fix quality string (GGA fix quality mapping).
    const char* fixQualStr = "—";
    switch (gps.fixQuality) {
      case 0: fixQualStr = "Invalid"; break;
      case 1: fixQualStr = "GNSS";    break;
      case 2: fixQualStr = "DGPS";    break;
      case 4: fixQualStr = "RTK Fix"; break;
      case 5: fixQualStr = "RTK Flt"; break;
      default: fixQualStr = "Other";  break;
    }

    // Human-readable accuracy source.
    const char* accSrcStr = "—";
    if      (gps.accSource == 1) accSrcStr = "GST";
    else if (gps.accSource == 2) accSrcStr = "HDOP-est";

    // Format UTC time as HH:MM:SS or "—".
    char tbuf[16];
    if (gps.timeValid) snprintf(tbuf, sizeof(tbuf), "%02u:%02u:%02u", gps.hour, gps.minute, gps.second);
    else               snprintf(tbuf, sizeof(tbuf), "—");

    // Format UTC date as YYYY-MM-DD or "—".
    char dbuf[16];
    if (gps.year && gps.month && gps.day) snprintf(dbuf, sizeof(dbuf), "%04u-%02u-%02u", gps.year, gps.month, gps.day);
    else                                  snprintf(dbuf, sizeof(dbuf), "—");

    // Create nested object doc["gps"].
    JsonObject gpsObj = doc["gps"].to<JsonObject>();

    // Validity rule:
    // You want "valid" to be true either when:
    // - gps.valid is true, OR
    // - fixQuality indicates RTK Fix (4) or RTK Float (5)
    gpsObj["valid"] = gps.valid || (gps.fixQuality == 4) || (gps.fixQuality == 5);
    gpsObj["age_ms"]           = gps.ageMs;
    gpsObj["lat"]              = gps.lat;
    gpsObj["lon"]              = gps.lon;
    gpsObj["speed_kmh"]        = gps.speedKmh;
    gpsObj["sats_used"]        = gps.satsUsed;
    gpsObj["hdop"]             = gps.hdop;
    gpsObj["hacc_m"]           = gps.hAcc_m;
    gpsObj["vacc_m"]           = gps.vAcc_m;
    gpsObj["acc_source"]       = accSrcStr;
    gpsObj["acc_source_code"]  = gps.accSource;
    gpsObj["fix_type"]         = fixTypeStr;
    gpsObj["fix_quality"]      = fixQualStr;
    gpsObj["fix_type_code"]    = gps.fixType;
    gpsObj["fix_quality_code"] = gps.fixQuality;
    gpsObj["time_utc"]         = tbuf;
    gpsObj["date_utc"]         = dbuf;

    // Satellite details for skyplot (from GSV sentences).
    gpsObj["satellites_in_view"] = gps.satCount;

    if (gps.satCount > 0) {
      // Constellation index → string expected by the web UI's renderSkyplot().
      static const char* const consNames[] = {
        "GPS", "GLONASS", "Galileo", "BeiDou", "Other"
      };

      JsonArray satsArr = gpsObj["satellites"].to<JsonArray>();
      for (uint8_t i = 0; i < gps.satCount; i++) {
        JsonObject sat = satsArr.add<JsonObject>();
        sat["constellation"] = consNames[
            (gps.sats[i].constellation <= 4) ? gps.sats[i].constellation : 4];
        sat["nr"]            = gps.sats[i].nr;
        sat["elevation"]     = gps.sats[i].elevation;
        sat["azimuth"]       = gps.sats[i].azimuth;
        sat["signal_power"]  = gps.sats[i].snr;
      }
    }
  }
  #endif

  // ---------------- internet ----------------
  {
    JsonObject net = doc["internet"].to<JsonObject>();
    net["reach"] = internet;
  }

  // Serialize JSON and send it to the client.
  // - shrinkToFit() reduces internal capacity (may reduce RAM footprint after building).
  // - serializeJson() writes into a String (heap allocation).
  // - Cache-Control no-store ensures browser always fetches fresh status.
  String output;
  doc.shrinkToFit();
  serializeJson(doc, output);

  s_server->sendHeader("Cache-Control", "no-store");
  s_server->send(200, "application/json", output);
}

// ------------- API: /api/restart -------------
//
// handleRestart():
// - Responds with HTTP 204 (No Content) to acknowledge the action
// - Delays briefly so the response can be transmitted
// - Calls ESP.restart() to reboot the MCU
static void handleRestart() {
  markRequestAndGetPrevAgeMs();

  // no-store to avoid caching the restart call
  s_server->sendHeader("Cache-Control", "no-store");

  // 204 indicates success with no response body.
  s_server->send(204, "text/plain", "");

  // Small delay to allow TCP stack to flush the response.
  delay(150);

  // Reboot the ESP32.
  ESP.restart();
}

// ------------- API: /api/config -------------
static void handleConfigGet() {
  markRequestAndGetPrevAgeMs();

  const GnssConfig& cfg = gnss_config_get();
  const GnssConfig defaults = gnss_config_defaults();

  JsonDocument doc;
  doc["rx_pin"] = cfg.rx_pin;
  doc["tx_pin"] = cfg.tx_pin;
  doc["baud"] = cfg.baud;

  // Check if config is locked via build flags
  #ifdef FORCE_HARDCODED_UART
    doc["locked"] = true;
  #else
    doc["locked"] = false;
  #endif

  JsonObject defObj = doc["defaults"].to<JsonObject>();
  defObj["rx_pin"] = defaults.rx_pin;
  defObj["tx_pin"] = defaults.tx_pin;
  defObj["baud"] = defaults.baud;

  String output;
  doc.shrinkToFit();
  serializeJson(doc, output);

  s_server->sendHeader("Cache-Control", "no-store");
  s_server->send(200, "application/json", output);
}

static void handleConfigPost() {
  markRequestAndGetPrevAgeMs();

  // Check if config is locked via build flags
  #ifdef FORCE_HARDCODED_UART
    s_server->send(403, "application/json", "{\"ok\":false,\"error\":\"UART config is locked via build flags\"}");
    return;
  #endif

  if (!s_server->hasArg("plain")) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing body\"}");
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, s_server->arg("plain"));
  if (err) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
    return;
  }

  if (!doc["rx_pin"].is<int>() || !doc["tx_pin"].is<int>() || !doc["baud"].is<uint32_t>()) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"rx_pin, tx_pin, and baud are required\"}");
    return;
  }

  GnssConfig cfg = gnss_config_get();
  cfg.rx_pin = doc["rx_pin"].as<int>();
  cfg.tx_pin = doc["tx_pin"].as<int>();
  cfg.baud = doc["baud"].as<uint32_t>();

  String error;
  if (!gnss_apply_runtime_config(cfg, &error)) {
    String response = String("{\"ok\":false,\"error\":\"") + error + "\"}";
    s_server->send(400, "application/json", response);
    return;
  }

  JsonDocument resp;
  resp["ok"] = true;
  resp["message"] = "Config saved";
  JsonObject cfgObj = resp["config"].to<JsonObject>();
  cfgObj["rx_pin"] = cfg.rx_pin;
  cfgObj["tx_pin"] = cfg.tx_pin;
  cfgObj["baud"] = cfg.baud;

  String output;
  resp.shrinkToFit();
  serializeJson(resp, output);
  s_server->sendHeader("Cache-Control", "no-store");
  s_server->send(200, "application/json", output);
}

// ------------- API: /api/wifi_config -------------
static void handleWifiConfigGet() {
  markRequestAndGetPrevAgeMs();

  WifiConfig cfg{};
  String error;
  const bool loaded = wifi_config_load(cfg, &error);

  JsonDocument doc;
  doc["loaded"] = loaded;
  if (!loaded) {
    doc["error"] = error;
  }

  // Check if config is locked via build flags
  #if FORCE_WIFI_SECRETS
    doc["locked"] = true;
  #else
    doc["locked"] = false;
  #endif

  // If file missing, fall back to compile-time values.
  if (!loaded) {
    cfg.ssid = STA_SSID;
    cfg.pass = STA_PASS;
    cfg.dhcp = false;
    cfg.ip = STA_IP;
    cfg.gw = STA_GW;
    cfg.subnet = STA_SUBNET;
    cfg.dns = STA_DNS;
  }

  doc["ssid"] = cfg.ssid;
  doc["pass"] = cfg.pass;
  doc["dhcp"] = cfg.dhcp;
  doc["ip"] = cfg.ip.toString();
  doc["gw"] = cfg.gw.toString();
  doc["subnet"] = cfg.subnet.toString();
  doc["dns"] = cfg.dns.toString();

  String output;
  doc.shrinkToFit();
  serializeJson(doc, output);

  s_server->sendHeader("Cache-Control", "no-store");
  s_server->send(200, "application/json", output);
}

static void handleWifiConfigPost() {
  markRequestAndGetPrevAgeMs();

  #if FORCE_WIFI_SECRETS
    s_server->send(403, "application/json", "{\"ok\":false,\"error\":\"WiFi config is locked via build flags\"}");
    return;
  #endif

  if (!s_server->hasArg("plain")) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing body\"}");
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, s_server->arg("plain"));
  if (err) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
    return;
  }

  if (!doc["ssid"].is<const char*>() || !doc["pass"].is<const char*>() || !doc["dhcp"].is<bool>()) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"ssid, pass, and dhcp are required\"}");
    return;
  }

  WifiConfig cfg{};
  cfg.ssid = doc["ssid"].as<String>();
  cfg.pass = doc["pass"].as<String>();
  cfg.dhcp = doc["dhcp"].as<bool>();

  if (cfg.ssid.isEmpty()) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"ssid is empty\"}");
    return;
  }

  if (!cfg.dhcp) {
    if (!doc["ip"].is<const char*>() || !doc["gw"].is<const char*>() ||
        !doc["subnet"].is<const char*>() || !doc["dns"].is<const char*>()) {
      s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"ip, gw, subnet, and dns are required when dhcp is false\"}");
      return;
    }

    if (!cfg.ip.fromString(doc["ip"].as<String>()) ||
        !cfg.gw.fromString(doc["gw"].as<String>()) ||
        !cfg.subnet.fromString(doc["subnet"].as<String>()) ||
        !cfg.dns.fromString(doc["dns"].as<String>())) {
      s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid IP fields\"}");
      return;
    }
  } else {
    cfg.ip = IPAddress(0, 0, 0, 0);
    cfg.gw = IPAddress(0, 0, 0, 0);
    cfg.subnet = IPAddress(0, 0, 0, 0);
    cfg.dns = IPAddress(0, 0, 0, 0);
  }

  String error;
  if (!wifi_config_save(cfg, &error)) {
    String response = String("{\"ok\":false,\"error\":\"") + error + "\"}";
    s_server->send(400, "application/json", response);
    return;
  }

  JsonDocument resp;
  resp["ok"] = true;
  resp["message"] = "WiFi config saved";
  JsonObject cfgObj = resp["config"].to<JsonObject>();
  cfgObj["ssid"] = cfg.ssid;
  cfgObj["pass"] = cfg.pass;
  cfgObj["dhcp"] = cfg.dhcp;
  cfgObj["ip"] = cfg.ip.toString();
  cfgObj["gw"] = cfg.gw.toString();
  cfgObj["subnet"] = cfg.subnet.toString();
  cfgObj["dns"] = cfg.dns.toString();

  String output;
  resp.shrinkToFit();
  serializeJson(resp, output);
  s_server->sendHeader("Cache-Control", "no-store");
  s_server->send(200, "application/json", output);
}

#if NTRIP_CLIENT_ENABLE
// ------------- API: /api/ntrip_config -------------
static void handleNtripConfigGet() {
  markRequestAndGetPrevAgeMs();

  JsonDocument doc;
  JsonDocument src;
  bool loaded = false;
  String error;

  if (!LittleFS.exists(kNtripConfigPath)) {
    error = "NTRIP config not found";
  } else {
    File file = LittleFS.open(kNtripConfigPath, "r");
    if (!file) {
      error = "Failed to open NTRIP config";
    } else {
      const DeserializationError err = deserializeJson(src, file);
      file.close();
      if (err) {
        error = "Invalid NTRIP config JSON";
      } else {
        loaded = true;
      }
    }
  }

  doc["loaded"] = loaded;
  if (!loaded) {
    doc["error"] = error;
  }

  JsonObject ntrip = doc["ntrip"].to<JsonObject>();
  JsonObject srcNtrip = src["ntrip"];
  ntrip["enabled"] = srcNtrip["enabled"] | false;
  ntrip["host"] = srcNtrip["host"] | "rtk2go.com";
  ntrip["port"] = srcNtrip["port"] | 2101;
  ntrip["mount"] = srcNtrip["mount"] | "MOUNT";
  ntrip["user"] = srcNtrip["user"] | "user";
  ntrip["pass"] = srcNtrip["pass"] | "pass";
  ntrip["max_tries"] = srcNtrip["max_tries"] | 5;
  ntrip["retry_delay_ms"] = srcNtrip["retry_delay_ms"] | 30000;
  ntrip["health_timeout_ms"] = srcNtrip["health_timeout_ms"] | 60000;
  ntrip["passive_sample_ms"] = srcNtrip["passive_sample_ms"] | 5000;
  ntrip["required_valid_frames"] = srcNtrip["required_valid_frames"] | 3;
  ntrip["buffer_size"] = srcNtrip["buffer_size"] | 1024;
  ntrip["connect_timeout_ms"] = srcNtrip["connect_timeout_ms"] | 5000;

  JsonObject lockout = doc["lockout"].to<JsonObject>();
  JsonObject srcLockout = src["lockout"];
  lockout["failed_attempts"] = srcLockout["failed_attempts"] | 0;
  lockout["abandoned"] = srcLockout["abandoned"] | false;
  lockout["last_config_hash"] = srcLockout["last_config_hash"] | "";

  String output;
  doc.shrinkToFit();
  serializeJson(doc, output);

  s_server->sendHeader("Cache-Control", "no-store");
  s_server->send(200, "application/json", output);
}

static void handleNtripConfigPost() {
  markRequestAndGetPrevAgeMs();

  if (!s_server->hasArg("plain")) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing body\"}");
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, s_server->arg("plain"));
  if (err) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
    return;
  }

  if (!doc["enabled"].is<bool>() ||
      !doc["host"].is<const char*>() ||
      !doc["port"].is<int>() ||
      !doc["mount"].is<const char*>() ||
      !doc["user"].is<const char*>() ||
      !doc["pass"].is<const char*>() ||
      !doc["max_tries"].is<int>() ||
      !doc["retry_delay_ms"].is<int>() ||
      !doc["health_timeout_ms"].is<int>() ||
      !doc["passive_sample_ms"].is<int>() ||
      !doc["required_valid_frames"].is<int>() ||
      !doc["buffer_size"].is<int>() ||
      !doc["connect_timeout_ms"].is<int>()) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"Missing required NTRIP fields\"}");
    return;
  }

  JsonDocument out;
  JsonObject ntrip = out["ntrip"].to<JsonObject>();
  ntrip["enabled"] = doc["enabled"].as<bool>();
  ntrip["host"] = doc["host"].as<const char*>();
  ntrip["port"] = doc["port"].as<int>();
  ntrip["mount"] = doc["mount"].as<const char*>();
  ntrip["user"] = doc["user"].as<const char*>();
  ntrip["pass"] = doc["pass"].as<const char*>();
  ntrip["max_tries"] = doc["max_tries"].as<int>();
  ntrip["retry_delay_ms"] = doc["retry_delay_ms"].as<int>();
  ntrip["health_timeout_ms"] = doc["health_timeout_ms"].as<int>();
  ntrip["passive_sample_ms"] = doc["passive_sample_ms"].as<int>();
  ntrip["required_valid_frames"] = doc["required_valid_frames"].as<int>();
  ntrip["buffer_size"] = doc["buffer_size"].as<int>();
  ntrip["connect_timeout_ms"] = doc["connect_timeout_ms"].as<int>();

  JsonDocument existing;
  JsonObject lockout = out["lockout"].to<JsonObject>();
  if (LittleFS.exists(kNtripConfigPath)) {
    File file = LittleFS.open(kNtripConfigPath, "r");
    if (file) {
      if (!deserializeJson(existing, file)) {
        JsonObject existingLockout = existing["lockout"];
        lockout["failed_attempts"] = existingLockout["failed_attempts"] | 0;
        lockout["abandoned"] = existingLockout["abandoned"] | false;
        lockout["last_config_hash"] = existingLockout["last_config_hash"] | "";
      }
      file.close();
    }
  }
  if (!lockout.containsKey("failed_attempts")) {
    lockout["failed_attempts"] = 0;
    lockout["abandoned"] = false;
    lockout["last_config_hash"] = "";
  }

  File outFile = LittleFS.open(kNtripConfigPath, "w");
  if (!outFile) {
    s_server->send(500, "application/json", "{\"ok\":false,\"error\":\"Failed to open NTRIP config for writing\"}");
    return;
  }
  serializeJson(out, outFile);
  outFile.close();

  JsonDocument resp;
  resp["ok"] = true;
  resp["message"] = "NTRIP config saved";
  resp["ntrip"] = ntrip;

  String output;
  resp.shrinkToFit();
  serializeJson(resp, output);
  s_server->sendHeader("Cache-Control", "no-store");
  s_server->send(200, "application/json", output);
}
#endif

// webui_begin():
// Called once from main.cpp to register routes and provide dependencies.
// Parameters:
// - server:  reference to the WebServer instance used in loop()
// - sta_dns: the DNS IP you want to display in status
void webui_begin(WebServer& server, const IPAddress& sta_dns) {
  // Store pointers/globals so handlers can access the server and DNS info.
  s_server = &server;
  s_sta_dns = sta_dns;

  // -------- HTTP UI routes ----------
  // Serve HTML at "/"
  server.on("/", HTTP_GET, []() {
    markRequestAndGetPrevAgeMs();
    sendProgmemGzip(200, "text/html; charset=utf-8", APP_INDEX, APP_INDEX_LEN, "no-store");
  });

  // Serve CSS at "/style.css"
  // Cache it for 1 day to reduce repeated transfers.
  server.on("/style.css", HTTP_GET, []() {
    markRequestAndGetPrevAgeMs();
    sendProgmemGzip(200, "text/css; charset=utf-8", APP_STYLE, APP_STYLE_LEN, "public, max-age=86400");
  });

  // Serve JS at "/app.js"
  // No-store to avoid stale UI behavior after firmware updates.
  server.on("/app.js", HTTP_GET, []() {
    markRequestAndGetPrevAgeMs();
    sendProgmemGzip(200, "application/javascript; charset=utf-8", APP_JS, APP_JS_LEN, "no-store");
  });

  // Serve favicon at "/favicon.ico"
  // Cache it for 7 days.
  server.on("/favicon.ico", HTTP_GET, []() {
    markRequestAndGetPrevAgeMs();
    sendProgmemGzip(200, "image/x-icon", APP_FAVICON, APP_FAVICON_LEN, "public, max-age=604800");
  });

  // JSON status endpoint
  server.on("/api/status", HTTP_GET, handleStatus);

  // Config endpoints
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/wifi_config", HTTP_GET, handleWifiConfigGet);
  server.on("/api/wifi_config", HTTP_POST, handleWifiConfigPost);
#if NTRIP_CLIENT_ENABLE
  server.on("/api/ntrip_config", HTTP_GET, handleNtripConfigGet);
  server.on("/api/ntrip_config", HTTP_POST, handleNtripConfigPost);
#endif

  // Restart endpoint (POST)
  server.on("/api/restart", HTTP_POST, handleRestart);

  // Default handler for unknown paths.
  // You currently just count the request and do not respond (send is commented).
  server.onNotFound([]() {
    markRequestAndGetPrevAgeMs();
    //server.send(404, "text/plain", "404 Not Found");
  });
}

#else

#include "web_ui.h"

void webui_begin(WebServer& server, const IPAddress& sta_dns) {
  (void)server;
  (void)sta_dns;
}

#endif
