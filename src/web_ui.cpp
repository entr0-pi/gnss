// ======================= web_ui.cpp (FINAL - JSON built here) =======================
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <cstring>
#include <ArduinoJson.h>

#include "index_html.h"
#include "style_css.h"
#include "favicon_ico.h"
#include "web_ui.h"

// Keep a pointer so helpers/handlers can use the same server instance
static WebServer* s_server = nullptr;

// Keep DNS for status
static IPAddress s_sta_dns;

// ---------------- Simple HTTP stats ----------------
static uint32_t g_http_req_total = 0;
static uint32_t g_http_last_req_ms = 0;

static uint32_t markRequestAndGetPrevAgeMs() {
  const uint32_t now = millis();
  uint32_t age = 0;
  if (g_http_last_req_ms != 0) age = now - g_http_last_req_ms;
  g_http_last_req_ms = now;
  g_http_req_total++;
  return age;
}

// Serve a PROGMEM buffer (text or binary)
static void sendProgmem(int code,
                        const char* contentType,
                        const uint8_t* data,
                        size_t len,
                        const char* cacheControl) {
  s_server->sendHeader("Cache-Control", cacheControl);
  s_server->send_P(code, contentType, (const char*)data, len);
}

// ------------- API: internet reachable -------------
static bool logInternetHTTP() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[NET] WiFi not connected");
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(2500);
  http.setReuse(false);

  const char* url = "http://connectivitycheck.gstatic.com/generate_204";

  if (!http.begin(client, url)) {
    Serial.println("[NET] http.begin() failed");
    return false;
  }

  int code = http.GET();
  Serial.print("[NET] HTTP code: ");
  Serial.println(code);

  bool ok = false;
  if (code == 204) {
    Serial.println("[NET] Internet reachable");
    ok = true;
  } else if (code > 0) {
    Serial.println("[NET] Reached server, but unexpected code");
    ok = false;
  } else {
    Serial.print("[NET] HTTP GET failed, err=");
    Serial.println(http.errorToString(code)); // code is negative on error
    ok = false;
  }

  http.end();
  return ok;
}

// ------------- API: /api/status -------------
static void handleStatus() {
  const uint32_t prev_age_ms = markRequestAndGetPrevAgeMs();
  const uint32_t now = millis();

  // --- Device ---
  const uint32_t uptime_ms = now;
  const uint32_t cpu_mhz   = ESP.getCpuFreqMHz();

  // --- Memory ---
  const uint32_t heap_free      = ESP.getFreeHeap();
  const uint32_t heap_min_free  = ESP.getMinFreeHeap();
  const uint32_t heap_max_alloc = ESP.getMaxAllocHeap();

  // --- WiFi (STA) ---
  const String ssid  = WiFi.SSID();
  const String ip    = WiFi.localIP().toString();
  const String gw    = WiFi.gatewayIP().toString();
  const String dns   = s_sta_dns.toString();
  const String mask  = WiFi.subnetMask().toString();
  const String bcast = WiFi.broadcastIP().toString();
  const int32_t rssi = WiFi.RSSI();
  const String mac   = WiFi.macAddress();

  // --- Internet ---
  bool internet;
  if (logInternetHTTP()) {
    internet  = true; // "✅" in html now
  }
  else {
    internet  = false; // "❌" in html now
  }

  // --- HTTP ---
  const uint16_t http_port = 80;

  // --- App ---
  const char* app_state = "idle";
  const char* app_notes = "ready";

  // --- BLE snapshot (optional) ---
  WebuiBleSnapshot ble{};
  const bool has_ble = webui_get_ble_snapshot(ble);

  // --- GPS snapshot (optional) ---
  #if NMEA_ENABLE
  WebuiGpsSnapshot gps{};
  const bool has_gps = webui_get_gps_snapshot(gps);
  #endif

  // --- JSON ---
  // ArduinoJson v7
  JsonDocument doc;

  // ---------------- device ----------------
  
    JsonObject device = doc["device"].to<JsonObject>();
    device["uptime_ms"] = uptime_ms;
    device["cpu_mhz"]   = cpu_mhz;

    // build string once
    char build[32];
    snprintf(build, sizeof(build), "%s %s", __DATE__, __TIME__);
    device["build"] = build;
  

  // ---------------- memory ----------------
  
    JsonObject mem = doc["memory"].to<JsonObject>();
    mem["heap_free"]      = heap_free;
    mem["heap_min_free"]  = heap_min_free;
    mem["heap_max_alloc"] = heap_max_alloc;
  

  // ---------------- wifi ----------------
  
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["mode"]      = "STA";
    wifi["ssid"]      = ssid;
    wifi["ip"]        = ip;
    wifi["gw"]        = gw;
    wifi["dns"]       = dns;
    wifi["subnet"]    = mask;
    wifi["broadcast"] = bcast;
    wifi["rssi_dbm"]  = rssi;
    wifi["mac"]       = mac;
  

  // ---------------- http ----------------
  
    JsonObject http = doc["http"].to<JsonObject>();
    http["port"]            = http_port;
    http["req_total"]       = g_http_req_total;
    http["prev_req_age_ms"] = prev_age_ms;
  

  // ---------------- app ----------------
  
    JsonObject app = doc["app"].to<JsonObject>();
    app["state"] = app_state;
    app["notes"] = app_notes;
  

  // ---------------- ble (optional) ----------------
  if (has_ble) {
    JsonObject bleObj = doc["ble"].to<JsonObject>();
    bleObj["connected"] = ble.connected; // "✅" : "❌" in html now
    bleObj["mtu"]       = ble.mtu;
    bleObj["txBytes"]   = ble.txBytes;
    bleObj["rxBytes"]   = ble.rxBytes;
  }

  // ---------------- gps (optional) ----------------
  
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

    char tbuf[16];
    if (gps.timeValid) snprintf(tbuf, sizeof(tbuf), "%02u:%02u:%02u", gps.hour, gps.minute, gps.second);
    else               snprintf(tbuf, sizeof(tbuf), "—");

    char dbuf[16];
    if (gps.year && gps.month && gps.day) snprintf(dbuf, sizeof(dbuf), "%04u-%02u-%02u", gps.year, gps.month, gps.day);
    else                                  snprintf(dbuf, sizeof(dbuf), "—");

    JsonObject gpsObj = doc["gps"].to<JsonObject>();
    gpsObj["valid"]            = gps.valid || (gps.fixQuality = 4) || (gps.fixQuality = 5); // "✅" : "❌" in html now. UM980  hard rule validity soften to include RTK
    gpsObj["age_ms"]           = gps.ageMs;
    gpsObj["lat"]              = gps.lat;
    gpsObj["lon"]              = gps.lon;
    gpsObj["speed_kmh"]        = gps.speedKmh;
    gpsObj["sats_used"]        = gps.satsUsed;
    gpsObj["hdop"]             = gps.hdop;
    gpsObj["fix_type"]         = fixTypeStr;
    gpsObj["fix_quality"]      = fixQualStr;
    gpsObj["fix_type_code"]    = gps.fixType;
    gpsObj["fix_quality_code"] = gps.fixQuality;
    gpsObj["time_utc"]         = tbuf;
    gpsObj["date_utc"]         = dbuf;
  }
  #endif

  // ---------------- internet ----------------
  
    JsonObject net = doc["internet"].to<JsonObject>();
    net["reach"] = internet;
  


  // Serialize JSON and then send to the client in a string
  String output;
  doc.shrinkToFit();
  serializeJson(doc, output);
  s_server->sendHeader("Cache-Control", "no-store");
  s_server->send(200, "application/json", output);

}

// ------------- API: /api/restart -------------
static void handleRestart() {
  markRequestAndGetPrevAgeMs();
  s_server->sendHeader("Cache-Control", "no-store");
  s_server->send(204, "text/plain", "");
  delay(150);
  ESP.restart();
}

void webui_begin(WebServer& server, const IPAddress& sta_dns) {
  s_server = &server;
  s_sta_dns = sta_dns;

  // -------- HTTP UI routes ----------
  server.on("/", HTTP_GET, []() {
    markRequestAndGetPrevAgeMs();
    sendProgmem(200, "text/html; charset=utf-8", INDEX_HTML, INDEX_HTML_LEN, "no-store");
  });

  server.on("/style.css", HTTP_GET, []() {
    markRequestAndGetPrevAgeMs();
    sendProgmem(200, "text/css; charset=utf-8", STYLE_CSS, STYLE_CSS_LEN, "public, max-age=86400");
  });

  server.on("/favicon.ico", HTTP_GET, []() {
    markRequestAndGetPrevAgeMs();
    sendProgmem(200, "image/x-icon", FAVICON_ICO, FAVICON_ICO_LEN, "public, max-age=604800");
  });

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/restart", HTTP_POST, handleRestart);

  server.onNotFound([]() {
    markRequestAndGetPrevAgeMs();
    //server.send(404, "text/plain", "404 Not Found");
  });
}
