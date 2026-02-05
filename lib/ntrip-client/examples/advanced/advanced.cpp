/**
 * Advanced NTRIP Client Example
 * 
 * This example demonstrates:
 * - Real-time statistics display
 * - Error handling and recovery
 * - Dynamic configuration updates
 * - RTCM message type logging
 */

#include "NtripClient.h"
#include <WiFi.h>

// WiFi credentials
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

NtripClient ntripClient;
bool isInternetReachable = false;

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17); // RX=16, TX=17
  
  delay(1000);
  Serial.println("\n\n=== NTRIP Client Advanced Example ===\n");
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  isInternetReachable = true;
  
  // Configure NTRIP
  NtripConfig config;
  config.host = "rtk2go.com";           // Your caster
  config.port = 2101;
  config.mount = "YOUR_MOUNT";          // Your mount point
  config.user = "user@example.com";     // Your email/user
  config.pass = "none";
  config.maxTries = 5;
  
  // Advanced settings
  config.retryDelayMs = 30000;          // 30 second retry
  config.healthTimeoutMs = 60000;       // 60 second zombie timeout
  config.passiveSampleMs = 5000;        // Check health every 5s
  config.requiredValidFrames = 3;       // Need 3 valid frames
  config.bufferSize = 2048;             // 2KB buffer for high-rate streams
  
  // Initialize and start
  if (ntripClient.begin(config, Serial2)) {
    Serial.println("NTRIP client initialized!");
    ntripClient.startTask(0); // Run on Core 0
  } else {
    Serial.println("Failed to initialize NTRIP client!");
  }
}

void loop() {
  // Update internet status based on WiFi
  isInternetReachable = (WiFi.status() == WL_CONNECTED);
  
  // Display status every 10 seconds
  static unsigned long lastDisplay = 0;
  if (millis() - lastDisplay > 10000) {
    lastDisplay = millis();
    displayStatus();
  }
  
  // Handle errors
  if (ntripClient.state() == NtripState::LOCKED_OUT) {
    handleLockout();
  }
  
  delay(1000);
}

void displayStatus() {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║        NTRIP CLIENT STATUS             ║");
  Serial.println("╚════════════════════════════════════════╝");
  
  // Connection state
  NtripState state = ntripClient.state();
  Serial.print("State:         ");
  switch(state) {
    case NtripState::DISCONNECTED:
      Serial.println("DISCONNECTED");
      break;
    case NtripState::CONNECTING:
      Serial.println("CONNECTING...");
      break;
    case NtripState::STREAMING:
      Serial.print("STREAMING ");
      Serial.println(ntripClient.isHealthy() ? "(HEALTHY)" : "(VALIDATING)");
      break;
    case NtripState::LOCKED_OUT:
      Serial.println("LOCKED OUT");
      break;
  }
  
  // Statistics
  NtripStats stats = ntripClient.getStats();
  
  if (state == NtripState::STREAMING || state == NtripState::LOCKED_OUT) {
    Serial.printf("Uptime:        %lu seconds\n", stats.totalUptime / 1000);
    Serial.printf("Valid Frames:  %lu\n", stats.totalFrames);
    Serial.printf("CRC Errors:    %lu\n", stats.crcErrors);
    Serial.printf("Data RX:       %.2f KB\n", stats.bytesReceived / 1024.0);
    Serial.printf("Reconnects:    %lu\n", stats.reconnects);
    
    if (stats.lastMessageType > 0) {
      Serial.printf("Last RTCM:     %d (", stats.lastMessageType);
      printMessageName(stats.lastMessageType);
      Serial.println(")");
      
      unsigned long ageMs = millis() - stats.lastFrameTime;
      Serial.printf("Frame Age:     %lu.%03lu seconds\n", 
                    ageMs / 1000, ageMs % 1000);
    }
    
    if (stats.totalUptime > 0) {
      float bandwidth = (float)stats.bytesReceived / (stats.totalUptime / 1000.0);
      Serial.printf("Avg Rate:      %.2f bytes/sec\n", bandwidth);
    }
    
    // Error information
    if (stats.lastError != NtripError::NONE) {
      Serial.print("Last Error:    ");
      Serial.println(stats.lastErrorMessage);
    }
  }
  
  Serial.println("════════════════════════════════════════\n");
}

void printMessageName(uint16_t msgType) {
  switch(msgType) {
    case 1005: Serial.print("Station Position"); break;
    case 1074: Serial.print("GPS MSM4"); break;
    case 1077: Serial.print("GPS MSM7"); break;
    case 1084: Serial.print("GLONASS MSM4"); break;
    case 1087: Serial.print("GLONASS MSM7"); break;
    case 1094: Serial.print("Galileo MSM4"); break;
    case 1097: Serial.print("Galileo MSM7"); break;
    case 1124: Serial.print("BeiDou MSM4"); break;
    case 1127: Serial.print("BeiDou MSM7"); break;
    case 1230: Serial.print("GLONASS Biases"); break;
    default: Serial.print("Unknown");
  }
}

void handleLockout() {
  static bool lockoutLogged = false;
  
  if (!lockoutLogged) {
    Serial.println("\n⚠️  CLIENT LOCKED OUT ⚠️");
    Serial.println("Too many connection failures.");
    
    NtripError err = ntripClient.getLastError();
    Serial.print("Reason: ");
    Serial.println(ntripClient.getErrorMessage());
    
    switch(err) {
      case NtripError::HTTP_AUTH_FAILED:
        Serial.println("\n💡 Check your username and password");
        Serial.println("   Some casters require email address as username");
        break;
      case NtripError::HTTP_MOUNT_NOT_FOUND:
        Serial.println("\n💡 Verify mount point name (case-sensitive)");
        Serial.println("   Check caster's source table");
        break;
      case NtripError::TCP_CONNECT_FAILED:
        Serial.println("\n💡 Check network connectivity");
        Serial.println("   Verify host and port are correct");
        break;
      default:
        Serial.println("\n💡 Will retry automatically...");
    }
    
    lockoutLogged = true;
  }
  
  // Auto-reset after 2 minutes
  static unsigned long lockoutStart = millis();
  if (millis() - lockoutStart > 120000) {
    Serial.println("\n🔄 Auto-resetting lockout...");
    ntripClient.reset();
    lockoutLogged = false;
    lockoutStart = millis();
  }
}
