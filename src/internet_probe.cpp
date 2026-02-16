#include "app.h"
#define MODULE_LOG 1
#include "logger.h"

#if WIFI_ENABLE

#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>

#include "internet_probe.h"

static bool     g_internet_reachable = false;
static uint32_t g_internet_probe_ms  = 0;

static bool runInternetHttpProbe() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_W("NET", "WiFi not connected");
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(1000);
  http.setReuse(false);

  const char* url = "http://connectivitycheck.gstatic.com/generate_204";
  if (!http.begin(client, url)) {
    LOG_E("NET", "http.begin() failed");
    return false;
  }

  int code = http.GET();
  LOG_I("NET", "HTTP code: %d", code);

  bool ok = false;
  if (code == 204) {
    LOG_I("NET", "Internet reachable");
    ok = true;
  } else if (code > 0) {
    LOG_W("NET", "Reached server, but unexpected code");
  } else {
    LOG_W("NET", "HTTP GET failed, err=%s", http.errorToString(code).c_str());
  }

  http.end();
  return ok;
}

bool internet_probe_is_reachable() {
  const uint32_t now = millis();
  if (g_internet_probe_ms == 0 || (now - g_internet_probe_ms) >= 10000) {
    g_internet_reachable = runInternetHttpProbe();
    g_internet_probe_ms = now;
  }
  return g_internet_reachable;
}

#endif
