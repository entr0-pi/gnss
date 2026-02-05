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

// --- 3. Configuration Monitor Task (Core 1) ---

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
    
    // Display statistics every 30 seconds
    if (wasConfigured && millis() - lastStatsDisplay > 30000) {
      lastStatsDisplay = millis();
      
      NtripStats stats = ntripClient.getStats();
      
      Serial.println(F("\n========== NTRIP Statistics =========="));
      Serial.printf("Uptime:        %lu seconds\n", stats.totalUptime / 1000);
      Serial.printf("Frames:        %lu valid, %lu CRC errors\n", 
                    stats.totalFrames, stats.crcErrors);
      Serial.printf("Data received: %lu KB\n", stats.bytesReceived / 1024);
      Serial.printf("Reconnects:    %lu\n", stats.reconnects);
      Serial.printf("Last RTCM:     %d (%lu ms ago)\n", 
                    stats.lastMessageType,
                    stats.lastFrameTime > 0 ? millis() - stats.lastFrameTime : 0);
      
      if (stats.lastError != NtripError::NONE) {
        Serial.printf("Last error:    %s\n", stats.lastErrorMessage.c_str());
      }
      
      Serial.println(F("======================================\n"));
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// --- 4. Main Application Entry Points ---

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17); // RX=16, TX=17 (adjust for your board)
  
  Serial.println(F("\n\n"));
  Serial.println(F("╔════════════════════════════════════════╗"));
  Serial.println(F("║   NTRIP Client v2.0 - Production       ║"));
  Serial.println(F("║   ESP32 RTK Correction Client          ║"));
  Serial.println(F("╚════════════════════════════════════════╝"));
  Serial.println();
  
  // Initialize LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println(F("[FS] ❌ LittleFS mount failed!"));
    return;
  }
  Serial.println(F("[FS] ✓ LittleFS mounted"));

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
    }
  } else {
    Serial.println(F("[FS] ✓ Config file exists"));
  }

  // Start the NTRIP client task on Core 0
  ntripClient.startTask(0);

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
  Serial.println(F("[BOOT] ✓ Config monitor started\n"));
}

void loop() {
  // Main loop runs on Core 1
  // Display real-time status
  
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
    }
    
    Serial.println();
  }
  
  vTaskDelay(pdMS_TO_TICKS(100));
}
