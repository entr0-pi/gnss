/**
 * Minimal NTRIP Client Example
 * 
 * Bare minimum code to get RTK corrections flowing to your GNSS receiver.
 * Perfect for testing or embedding in larger projects.
 */

#include "NtripClient.h"
#include <WiFi.h>

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

NtripClient ntrip;

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200);  // GNSS receiver on Serial2
  
  // Connect WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi OK");
  
  // Configure NTRIP
  NtripConfig cfg;
  cfg.host = "rtk2go.com";
  cfg.port = 2101;
  cfg.mount = "YOUR_MOUNT_POINT";
  cfg.user = "your@email.com";
  cfg.pass = "none";
  
  // Start
  ntrip.begin(cfg, Serial2);
  ntrip.startTask(0);
  
  Serial.println("NTRIP started!");
}

void loop() {
  // Print status every 5 seconds
  static unsigned long last = 0;
  if (millis() - last > 5000) {
    last = millis();
    
    if (ntrip.isHealthy()) {
      NtripStats s = ntrip.getStats();
      Serial.printf("✅ RTK OK | %lu frames | %lu KB\n", 
                    s.totalFrames, s.bytesReceived / 1024);
    } else {
      Serial.println("⏸️  Waiting for corrections...");
    }
  }
  
  delay(1000);
}
