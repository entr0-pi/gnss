/**
 * NTRIP Client — Basic Example
 *
 * Self-contained example demonstrating:
 * - WiFi connection
 * - Logger callback
 * - Status monitoring with statistics
 * - Lockout auto-recovery
 *
 * No external dependencies — compiles standalone.
 */

#include "NtripClient.h"
#include <WiFi.h>

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

NtripClient ntrip;

// Logger callback — routes library logs to Serial
static void ntripLog(NtripLogLevel level, const char* tag, const char* message) {
  const char* lvl = "?";
  switch (level) {
    case NtripLogLevel::Error:   lvl = "E"; break;
    case NtripLogLevel::Warning: lvl = "W"; break;
    case NtripLogLevel::Info:    lvl = "I"; break;
    case NtripLogLevel::Debug:   lvl = "D"; break;
  }
  Serial.printf("[%s][%s] %s\n", lvl, tag, message);
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17);  // GNSS receiver (adjust pins)

  delay(1000);
  Serial.println("\n=== NTRIP Client Basic Example ===\n");

  // Connect WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWiFi connected (%s)\n\n", WiFi.localIP().toString().c_str());

  // Configure
  NtripConfig cfg;
  cfg.host  = "rtk2go.com";
  cfg.port  = 2101;
  cfg.mount = "YOUR_MOUNT_POINT";
  cfg.user  = "your@email.com";
  cfg.pass  = "none";

  // Optional tuning
  cfg.maxTries        = 5;
  cfg.retryDelayMs    = 30000;
  cfg.healthTimeoutMs = 60000;
  cfg.bufferSize      = 2048;

  // Attach logger
  ntrip.setLogger(ntripLog);

  // Start
  if (!ntrip.begin(cfg, Serial2)) {
    Serial.println("begin() failed — check configuration");
    return;
  }
  ntrip.startTask(0);
}

void loop() {
  // Print status every 10 seconds
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint < 10000) {
    delay(1000);
    return;
  }
  lastPrint = millis();

  NtripState st = ntrip.state();
  NtripStats stats = ntrip.getStats();

  // Status line
  Serial.print("[NTRIP] ");
  switch (st) {
    case NtripState::DISCONNECTED: Serial.print("DISCONNECTED"); break;
    case NtripState::CONNECTING:   Serial.print("CONNECTING");   break;
    case NtripState::STREAMING:
      Serial.print(ntrip.isHealthy() ? "STREAMING (healthy)" : "STREAMING (validating)");
      break;
    case NtripState::LOCKED_OUT:   Serial.print("LOCKED OUT");   break;
  }

  if (st == NtripState::STREAMING || stats.totalFrames > 0) {
    Serial.printf(" | %lu frames | %lu KB | RTCM%d",
        stats.totalFrames,
        stats.bytesReceived / 1024,
        stats.lastMessageType);
  }
  Serial.println();

  // Auto-reset lockout after 2 minutes
  static unsigned long lockoutStart = 0;
  if (st == NtripState::LOCKED_OUT) {
    if (lockoutStart == 0) {
      lockoutStart = millis();
      Serial.printf("[NTRIP] Locked out: %s\n", ntrip.getErrorMessage().c_str());
    }
    if (millis() - lockoutStart > 120000) {
      Serial.println("[NTRIP] Auto-resetting lockout");
      ntrip.reset();
      lockoutStart = 0;
    }
  } else {
    lockoutStart = 0;
  }
}
