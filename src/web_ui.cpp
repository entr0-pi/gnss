// ======================= web_ui.cpp (FINAL - JSON built here) =======================
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>

#include "web_ui.h"
#include "index_html.h"
#include "style_css.h"
#include "favicon_ico.h"

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
    Serial.println("[NET] Internet reachable ✅");
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
  const char* internet;
  if (logInternetHTTP()) {
    internet  = "✅";
  }
  else {
    internet  = "❌";
  }

  // --- HTTP ---
  const uint16_t http_port = 80;

  // --- App ---
  const char* app_state = "idle";
  const char* app_notes = "ready";

  // --- BLE snapshot (optional) ---
  WebuiBleSnapshot ble{};
  const bool has_ble = webui_get_ble_snapshot(ble);

  // --- JSON ---
  String json;
  json.reserve(1300);

  json += "{";

  json += "\"device\":{";
  json += "\"uptime_ms\":" + String(uptime_ms) + ",";
  json += "\"cpu_mhz\":" + String(cpu_mhz) + ",";
  json += "\"build\":\"" + String(__DATE__) + " " + String(__TIME__) + "\"";
  json += "},";

  json += "\"memory\":{";
  json += "\"heap_free\":" + String(heap_free) + ",";
  json += "\"heap_min_free\":" + String(heap_min_free) + ",";
  json += "\"heap_max_alloc\":" + String(heap_max_alloc);
  json += "},";

  json += "\"wifi\":{";
  json += "\"mode\":\"STA\",";
  json += "\"ssid\":\"" + ssid + "\",";
  json += "\"ip\":\"" + ip + "\",";
  json += "\"gw\":\"" + gw + "\",";
  json += "\"dns\":\"" + dns + "\",";
  json += "\"subnet\":\"" + mask + "\",";
  json += "\"broadcast\":\"" + bcast + "\",";
  json += "\"rssi_dbm\":" + String(rssi) + ",";
  json += "\"mac\":\"" + mac + "\"";
  json += "},";

  json += "\"http\":{";
  json += "\"port\":" + String(http_port) + ",";
  json += "\"req_total\":" + String(g_http_req_total) + ",";
  json += "\"prev_req_age_ms\":" + String(prev_age_ms);
  json += "},";

  json += "\"app\":{";
  json += "\"state\":\"" + String(app_state) + "\",";
  json += "\"notes\":\"" + String(app_notes) + "\"";
  json += "},";

  // ---- BLE section added, JSON built here for consistency ----
  if (has_ble) {
    json += "\"ble\":{";
    json += "\"connected\":\"" + String(ble.connected ? "✅" : "❌") + "\",";
    json += "\"mtu\":"       + String(ble.mtu) + ",";
    json += "\"txBytes\":"   + String(ble.txBytes) + ",";
    json += "\"rxBytes\":"   + String(ble.rxBytes);
    json += "},";
  }

  json += "\"internet\":{";
  json += "\"reach\":\"" + String(internet) + "\"";
  json += "}";

  json += "}";

  s_server->sendHeader("Cache-Control", "no-store");
  s_server->send(200, "application/json", json);
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
