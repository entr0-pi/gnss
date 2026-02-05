/**
 * NTRIP Client - Production Main with JSON Configuration
 * 
 * Features:
 * - JSON configuration stored on LittleFS
 * - Hot-reload on configuration changes
 * - Automatic lockout state management
 * - Real-time statistics display
 * - Multi-core task architecture
 * - Comprehensive error handling
 */

#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "NtripClient.h"

// --- Configuration & Global Handles ---
extern bool isInternetReachable;    // Defined in your main project files
const char* CONFIG_PATH = "/ntrip_config.json";

NtripClient ntripClient;            // The NTRIP client library
JsonDocument configDoc;             // Buffer for JSON config
TaskHandle_t NtripMonitorHandle = NULL;

// Global Status Flags
unsigned long lastConfigCheck = 0;
const unsigned long configCheckInterval = 5000; // Check config every 5s

// --- 1. JSON State Management ---

/**
 * Updates the JSON file on LittleFS only if values have changed.
 * This prevents unnecessary wear on the ESP32's Flash memory.
 */
void updateJsonState(int attempts, bool abandoned, String currentHash) {
  // Check if we actually need to write to Flash
  if (configDoc["lockout"]["failed_attempts"] == attempts && 
      configDoc["lockout"]["abandoned"] == abandoned &&
      configDoc["lockout"]["last_config_hash"] == currentHash) {
    return; 
  }

  configDoc["lockout"]["failed_attempts"] = attempts;
  configDoc["lockout"]["abandoned"] = abandoned;
  configDoc["lockout"]["last_config_hash"] = currentHash;

  File file = LittleFS.open(CONFIG_PATH, "w");
  if (file) {
    serializeJson(configDoc, file);
    file.close();
    Serial.println(F("[FS] Status updated to Flash."));
  }
}

// --- 2. Configuration Loading & Validation ---

/**
 * Loads configuration from JSON and checks if settings have changed.
 * Returns true if NTRIP should be started/restarted.
 */
bool loadAndValidateConfig(NtripConfig& config) {
  File file = LittleFS.open(CONFIG_PATH, "r");
  if (!file) { 
    Serial.println(F("[FS] Config file missing!")); 
    return false; 
  }
  
  DeserializationError error = deserializeJson(configDoc, file);
  file.close();
  
  if (error) {
    Serial.printf("[FS] JSON parse error: %s\n", error.c_str());
    return false;
  }

  // Check if NTRIP is enabled
  bool isEnabled = configDoc["ntrip"]["enabled"] | false;
  if (!isEnabled) {
    return false;
  }

  // Detect configuration changes
  String currentSettings;
  serializeJson(configDoc["ntrip"], currentSettings);
  String oldHash = configDoc["lockout"]["last_config_hash"] | "";
  bool abandoned = configDoc["lockout"]["abandoned"] | false;
  int attempts = configDoc["lockout"]["failed_attempts"] | 0;
  int maxTries = configDoc["ntrip"]["max_tries"] | 5;

  // Reset counters if configuration changed
  if (currentSettings != oldHash) {
    Serial.println(F("[CONFIG] New config detected. Resetting lockout."));
    attempts = 0;
    abandoned = false;
    updateJsonState(attempts, abandoned, currentSettings);
  }

  // Check if locked out
  if (abandoned) {
    Serial.println(F("[CONFIG] Locked out due to repeated failures"));
    return false;
  }

  // Populate the NtripConfig structure
  config.host = configDoc["ntrip"]["host"] | "rtk2go.com";
  config.port = configDoc["ntrip"]["port"] | 2101;
  config.mount = configDoc["ntrip"]["mount"] | "MOUNT";
  config.user = configDoc["ntrip"]["user"] | "user";
  config.pass = configDoc["ntrip"]["pass"] | "pass";
  config.maxTries = maxTries;
  
  // Advanced settings (optional in JSON)
  config.retryDelayMs = configDoc["ntrip"]["retry_delay_ms"] | 30000;
  config.healthTimeoutMs = configDoc["ntrip"]["health_timeout_ms"] | 60000;
  config.passiveSampleMs = configDoc["ntrip"]["passive_sample_ms"] | 5000;
  config.requiredValidFrames = configDoc["ntrip"]["required_valid_frames"] | 3;
  config.bufferSize = configDoc["ntrip"]["buffer_size"] | 1024;
  config.connectTimeoutMs = configDoc["ntrip"]["connect_timeout_ms"] | 5000;

  return true;
}

/**
 * Updates JSON state based on NtripClient status.
 */
void syncJsonWithClientState() {
  NtripState state = ntripClient.state();
  String currentSettings;
  serializeJson(configDoc["ntrip"], currentSettings);
  
  int attempts = configDoc["lockout"]["failed_attempts"] | 0;
  bool abandoned = configDoc["lockout"]["abandoned"] | false;
  int maxTries = configDoc["ntrip"]["max_tries"] | 5;

  // Update based on client state
  if (state == NtripState::STREAMING && ntripClient.isHealthy()) {
    // Success - reset failures
    if (attempts != 0 || abandoned != false) {
      updateJsonState(0, false, currentSettings);
    }
  } else if (state == NtripState::LOCKED_OUT) {
    // Client is locked out - sync to JSON
    if (!abandoned) {
      updateJsonState(maxTries, true, currentSettings);
    }
  }
}

// --- 3. Display Functions ---

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

void displayDetailedStats() {
  NtripStats stats = ntripClient.getStats();
  
  Serial.println(F("\n╔════════════════════════════════════════╗"));
  Serial.println(F("║        NTRIP STATISTICS                ║"));
  Serial.println(F("╚════════════════════════════════════════╝"));
  Serial.printf("Uptime:        %lu seconds\n", stats.totalUptime / 1000);
  Serial.printf("Valid Frames:  %lu\n", stats.totalFrames);
  Serial.printf("CRC Errors:    %lu (%.1f%%)\n", 
                stats.crcErrors,
                stats.totalFrames > 0 ? (100.0 * stats.crcErrors / (stats.totalFrames + stats.crcErrors)) : 0);
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
    
    if (stats.totalFrames > 0) {
      float framesPerSec = (float)stats.totalFrames / (stats.totalUptime / 1000.0);
      Serial.printf("Frame Rate:    %.2f frames/sec\n", framesPerSec);
    }
  }
  
  // Error information
  if (stats.lastError != NtripError::NONE) {
    Serial.print("Last Error:    ");
    Serial.println(stats.lastErrorMessage);
  }
  
  Serial.println(F("════════════════════════════════════════\n"));
}

void handleLockout() {
  static bool lockoutLogged = false;
  static unsigned long lockoutStart = 0;
  
  if (!lockoutLogged) {
    Serial.println(F("\n⚠️  CLIENT LOCKED OUT ⚠️"));
    Serial.println(F("Too many connection failures."));
    
    NtripError err = ntripClient.getLastError();
    Serial.print(F("Reason: "));
    Serial.println(ntripClient.getErrorMessage());
    
    switch(err) {
      case NtripError::HTTP_AUTH_FAILED:
        Serial.println(F("\n💡 Check your username and password in config file"));
        Serial.println(F("   Some casters require email address as username"));
        break;
      case NtripError::HTTP_MOUNT_NOT_FOUND:
        Serial.println(F("\n💡 Verify mount point name (case-sensitive)"));
        Serial.println(F("   Check caster's source table"));
        break;
      case NtripError::TCP_CONNECT_FAILED:
        Serial.println(F("\n💡 Check network connectivity"));
        Serial.println(F("   Verify host and port are correct"));
        break;
      default:
        Serial.println(F("\n💡 Edit /ntrip_config.json to fix configuration"));
        Serial.println(F("   Or wait for auto-reset in 2 minutes"));
    }
    
    lockoutLogged = true;
    lockoutStart = millis();
  }
  
  // Auto-reset after 2 minutes
  if (millis() - lockoutStart > 120000) {
    Serial.println(F("\n🔄 Auto-resetting lockout..."));
    ntripClient.reset();
    
    // Also reset in JSON
    String currentSettings;
    serializeJson(configDoc["ntrip"], currentSettings);
    updateJsonState(0, false, currentSettings);
    
    lockoutLogged = false;
  }
}

// --- 4. Configuration Monitor Task (Core 1) ---

/**
 * This task monitors the configuration file and restarts the NTRIP client
 * when settings change or when internet connectivity changes.
 */
void configMonitorTask(void* pvParameters) {
  NtripConfig currentConfig;
  bool wasInternetReachable = false;
  bool wasConfigured = false;
  unsigned long lastStatsDisplay = 0;

  for(;;) {
    // Check internet status
    if (isInternetReachable != wasInternetReachable) {
      wasInternetReachable = isInternetReachable;
      
      if (!isInternetReachable) {
        Serial.println(F("[MONITOR] Internet lost - stopping client"));
        ntripClient.stop();
        wasConfigured = false;
      } else {
        Serial.println(F("[MONITOR] Internet restored"));
      }
    }

    // Periodically check configuration
    if (millis() - lastConfigCheck > configCheckInterval) {
      lastConfigCheck = millis();
      
      if (isInternetReachable) {
        NtripConfig newConfig;
        bool shouldBeRunning = loadAndValidateConfig(newConfig);
        
        // Check if config changed
        bool configChanged = (newConfig.host != currentConfig.host ||
                             newConfig.port != currentConfig.port ||
                             newConfig.mount != currentConfig.mount ||
                             newConfig.user != currentConfig.user ||
                             newConfig.pass != currentConfig.pass ||
                             newConfig.maxTries != currentConfig.maxTries);
        
        if (shouldBeRunning && (!wasConfigured || configChanged)) {
          if (configChanged && wasConfigured) {
            Serial.println(F("[MONITOR] Configuration changed - restarting client"));
          } else {
            Serial.println(F("[MONITOR] Starting NTRIP client"));
          }
          
          ntripClient.stop(); // Stop if running
          vTaskDelay(pdMS_TO_TICKS(500));
          
          if (ntripClient.begin(newConfig, Serial2)) {
            currentConfig = newConfig;
            wasConfigured = true;
          }
        } else if (!shouldBeRunning && wasConfigured) {
          Serial.println(F("[MONITOR] NTRIP disabled - stopping client"));
          ntripClient.stop();
          wasConfigured = false;
        }
        
        // Sync JSON state with client
        if (wasConfigured) {
          syncJsonWithClientState();
        }
      }
    }
    
    // Display detailed statistics every 30 seconds
    if (wasConfigured && millis() - lastStatsDisplay > 30000) {
      lastStatsDisplay = millis();
      
      NtripState state = ntripClient.state();
      if (state == NtripState::STREAMING || state == NtripState::LOCKED_OUT) {
        displayDetailedStats();
      }
    }
    
    // Handle lockout state
    if (ntripClient.state() == NtripState::LOCKED_OUT) {
      handleLockout();
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// --- 5. Main Application Entry Points ---

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17); // RX=16, TX=17 (adjust for your board)
  
  delay(1000);
  
  Serial.println(F("\n\n"));
  Serial.println(F("╔════════════════════════════════════════╗"));
  Serial.println(F("║   NTRIP Client v2.0 - Production       ║"));
  Serial.println(F("║   ESP32 RTK Correction Client          ║"));
  Serial.println(F("║   JSON Configuration + LittleFS        ║"));
  Serial.println(F("╚════════════════════════════════════════╝"));
  Serial.println();
  
  // Initialize LittleFS
  Serial.print(F("[FS] Mounting LittleFS... "));
  if (!LittleFS.begin(true)) {
    Serial.println(F("❌ FAILED!"));
    Serial.println(F("[FS] Cannot continue without filesystem"));
    return;
  }
  Serial.println(F("✓ OK"));

  // Self-Healing: Create default config if file is missing
  if (!LittleFS.exists(CONFIG_PATH)) {
    Serial.println(F("[FS] Creating default configuration..."));
    File file = LittleFS.open(CONFIG_PATH, "w");
    if (file) {
      const char* templateJson = 
        "{\n"
        "  \"ntrip\": {\n"
        "    \"enabled\": false,\n"
        "    \"host\": \"rtk2go.com\",\n"
        "    \"port\": 2101,\n"
        "    \"mount\": \"YOUR_MOUNT\",\n"
        "    \"user\": \"your_email@example.com\",\n"
        "    \"pass\": \"none\",\n"
        "    \"max_tries\": 5,\n"
        "    \"retry_delay_ms\": 30000,\n"
        "    \"health_timeout_ms\": 60000,\n"
        "    \"passive_sample_ms\": 5000,\n"
        "    \"required_valid_frames\": 3,\n"
        "    \"buffer_size\": 1024,\n"
        "    \"connect_timeout_ms\": 5000\n"
        "  },\n"
        "  \"lockout\": {\n"
        "    \"failed_attempts\": 0,\n"
        "    \"abandoned\": false,\n"
        "    \"last_config_hash\": \"\"\n"
        "  }\n"
        "}";
      file.print(templateJson);
      file.close();
      Serial.println(F("[FS] ✓ Template created"));
      Serial.println(F("\n⚠️  IMPORTANT: Edit /ntrip_config.json with your settings!"));
      Serial.println(F("    Set 'enabled': true and configure your mount point\n"));
    } else {
      Serial.println(F("[FS] ❌ Failed to create config file!"));
    }
  } else {
    Serial.println(F("[FS] ✓ Config file exists"));
    
    // Display current configuration
    File file = LittleFS.open(CONFIG_PATH, "r");
    if (file) {
      DeserializationError error = deserializeJson(configDoc, file);
      file.close();
      
      if (!error) {
        Serial.println(F("\n[CONFIG] Current settings:"));
        Serial.printf("  Host:    %s:%d\n", 
                     configDoc["ntrip"]["host"].as<const char*>(),
                     configDoc["ntrip"]["port"].as<int>());
        Serial.printf("  Mount:   %s\n", configDoc["ntrip"]["mount"].as<const char*>());
        Serial.printf("  Enabled: %s\n", 
                     configDoc["ntrip"]["enabled"].as<bool>() ? "YES" : "NO");
        
        if (configDoc["lockout"]["abandoned"].as<bool>()) {
          Serial.println(F("  Status:  🔒 LOCKED OUT (edit config to reset)"));
        }
        Serial.println();
      }
    }
  }

  // Start the NTRIP client task on Core 0
  ntripClient.startTask(0);
  Serial.println(F("[BOOT] ✓ NTRIP task started on Core 0"));

  // Start the configuration monitor on Core 1
  xTaskCreatePinnedToCore(
    configMonitorTask,
    "ConfigMonitor",
    4096,
    NULL,
    1,
    &NtripMonitorHandle,
    1  // Core 1
  );
  Serial.println(F("[BOOT] ✓ Config monitor started on Core 1\n"));
  
  Serial.println(F("════════════════════════════════════════"));
  Serial.println(F("System ready. Monitoring NTRIP status..."));
  Serial.println(F("════════════════════════════════════════\n"));
}

void loop() {
  // Main loop runs on Core 1
  // Display compact real-time status
  
  static unsigned long lastStatusPrint = 0;
  if (millis() - lastStatusPrint > 5000) {
    lastStatusPrint = millis();
    
    NtripState state = ntripClient.state();
    bool healthy = ntripClient.isHealthy();
    bool streaming = ntripClient.isStreaming();
    
    // Compact status line
    Serial.print(F("[STATUS] "));
    
    switch(state) {
      case NtripState::DISCONNECTED:
        Serial.print(F("⏸️  DISCONNECTED"));
        if (!isInternetReachable) {
          Serial.print(F(" (No Internet)"));
        }
        break;
      case NtripState::CONNECTING:
        Serial.print(F("🔄 CONNECTING"));
        break;
      case NtripState::STREAMING:
        if (healthy) {
          Serial.print(F("✅ STREAMING"));
        } else {
          Serial.print(F("⚠️  VALIDATING"));
        }
        break;
      case NtripState::LOCKED_OUT:
        Serial.print(F("🔒 LOCKED_OUT"));
        break;
    }
    
    if (streaming) {
      NtripStats stats = ntripClient.getStats();
      Serial.printf(" | ⬇ %lu KB | 📡 RTCM%d", 
                    stats.bytesReceived / 1024,
                    stats.lastMessageType);
      
      if (stats.totalFrames > 0) {
        unsigned long ageMs = millis() - stats.lastFrameTime;
        if (ageMs < 10000) {
          Serial.printf(" | ✓ Fresh (%.1fs ago)", ageMs / 1000.0);
        } else {
          Serial.printf(" | ⚠ Stale (%lus ago)", ageMs / 1000);
        }
      }
    }
    
    Serial.println();
  }
  
  vTaskDelay(pdMS_TO_TICKS(100));
}
