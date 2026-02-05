#include "ntrip_client.h"

#if NTRIP_CLIENT_ENABLE

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "NtripClient.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if WEBUI_ENABLE
#include "web_ui.h"
#endif

namespace {
const char* kConfigPath = "/ntrip_config.json";

NtripClient g_ntripClient;
JsonDocument g_configDoc;
TaskHandle_t g_ntripMonitorHandle = nullptr;
bool g_ntripTaskStarted = false;

unsigned long g_lastConfigCheck = 0;
constexpr unsigned long kConfigCheckInterval = 5000;

class NtripStreamWriter : public Print {
 public:
  void setStreamBuffer(StreamBufferHandle_t stream) {
    stream_ = stream;
  }

  size_t write(uint8_t data) override {
    return write(&data, 1);
  }

  size_t write(const uint8_t* buffer, size_t size) override {
    if (!stream_ || !buffer || size == 0) {
      return 0;
    }
    const size_t sent = xStreamBufferSend(stream_, buffer, size, 0);
    if (sent < size) {
      drops_ += static_cast<uint32_t>(size - sent);
    }
    return sent;
  }

  uint32_t drops() const {
    return drops_;
  }

 private:
  StreamBufferHandle_t stream_ = nullptr;
  uint32_t drops_ = 0;
};

NtripStreamWriter g_ntripWriter;

bool isInternetReachable() {
#if WEBUI_ENABLE
  return webui_get_internet_reachable();
#else
  return WiFi.status() == WL_CONNECTED;
#endif
}

void updateJsonState(int attempts, bool abandoned, const String& currentHash) {
  if (g_configDoc["lockout"]["failed_attempts"] == attempts &&
      g_configDoc["lockout"]["abandoned"] == abandoned &&
      g_configDoc["lockout"]["last_config_hash"] == currentHash) {
    return;
  }

  g_configDoc["lockout"]["failed_attempts"] = attempts;
  g_configDoc["lockout"]["abandoned"] = abandoned;
  g_configDoc["lockout"]["last_config_hash"] = currentHash;

  File file = LittleFS.open(kConfigPath, "w");
  if (file) {
    serializeJson(g_configDoc, file);
    file.close();
    Serial.println(F("[NTRIP] Status updated to Flash."));
  }
}

bool loadAndValidateConfig(NtripConfig& config) {
  File file = LittleFS.open(kConfigPath, "r");
  if (!file) {
    Serial.println(F("[NTRIP] Config file missing!"));
    return false;
  }

  DeserializationError error = deserializeJson(g_configDoc, file);
  file.close();

  if (error) {
    Serial.printf("[NTRIP] JSON parse error: %s\n", error.c_str());
    return false;
  }

  const bool isEnabled = g_configDoc["ntrip"]["enabled"] | false;
  if (!isEnabled) {
    return false;
  }

  String currentSettings;
  serializeJson(g_configDoc["ntrip"], currentSettings);
  const String oldHash = g_configDoc["lockout"]["last_config_hash"] | "";
  bool abandoned = g_configDoc["lockout"]["abandoned"] | false;
  int attempts = g_configDoc["lockout"]["failed_attempts"] | 0;
  const int maxTries = g_configDoc["ntrip"]["max_tries"] | 5;

  if (currentSettings != oldHash) {
    Serial.println(F("[NTRIP] New config detected. Resetting lockout."));
    attempts = 0;
    abandoned = false;
    updateJsonState(attempts, abandoned, currentSettings);
  }

  if (abandoned) {
    Serial.println(F("[NTRIP] Locked out due to repeated failures"));
    return false;
  }

  config.host = g_configDoc["ntrip"]["host"] | "rtk2go.com";
  config.port = g_configDoc["ntrip"]["port"] | 2101;
  config.mount = g_configDoc["ntrip"]["mount"] | "MOUNT";
  config.user = g_configDoc["ntrip"]["user"] | "user";
  config.pass = g_configDoc["ntrip"]["pass"] | "pass";
  config.maxTries = maxTries;

  config.retryDelayMs = g_configDoc["ntrip"]["retry_delay_ms"] | 30000;
  config.healthTimeoutMs = g_configDoc["ntrip"]["health_timeout_ms"] | 60000;
  config.passiveSampleMs = g_configDoc["ntrip"]["passive_sample_ms"] | 5000;
  config.requiredValidFrames = g_configDoc["ntrip"]["required_valid_frames"] | 3;
  config.bufferSize = g_configDoc["ntrip"]["buffer_size"] | 1024;
  config.connectTimeoutMs = g_configDoc["ntrip"]["connect_timeout_ms"] | 5000;

  return true;
}

void syncJsonWithClientState() {
  const NtripState state = g_ntripClient.state();
  String currentSettings;
  serializeJson(g_configDoc["ntrip"], currentSettings);

  const int attempts = g_configDoc["lockout"]["failed_attempts"] | 0;
  const bool abandoned = g_configDoc["lockout"]["abandoned"] | false;
  const int maxTries = g_configDoc["ntrip"]["max_tries"] | 5;

  if (state == NtripState::STREAMING && g_ntripClient.isHealthy()) {
    if (attempts != 0 || abandoned != false) {
      updateJsonState(0, false, currentSettings);
    }
  } else if (state == NtripState::LOCKED_OUT) {
    if (!abandoned) {
      updateJsonState(maxTries, true, currentSettings);
    }
  }
}

void printMessageName(uint16_t msgType) {
  switch (msgType) {
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
  const NtripStats stats = g_ntripClient.getStats();

  Serial.println(F("\n╔════════════════════════════════════════╗"));
  Serial.println(F("║        NTRIP STATISTICS                ║"));
  Serial.println(F("╚════════════════════════════════════════╝"));
  Serial.printf("Uptime:        %lu seconds\n", stats.totalUptime / 1000);
  Serial.printf("Valid Frames:  %lu\n", stats.totalFrames);
  Serial.printf("CRC Errors:    %lu (%.1f%%)\n",
                stats.crcErrors,
                stats.totalFrames > 0
                    ? (100.0 * stats.crcErrors / (stats.totalFrames + stats.crcErrors))
                    : 0);
  Serial.printf("Data RX:       %.2f KB\n", stats.bytesReceived / 1024.0);
  Serial.printf("Reconnects:    %lu\n", stats.reconnects);

  if (stats.lastMessageType > 0) {
    Serial.printf("Last RTCM:     %d (", stats.lastMessageType);
    printMessageName(stats.lastMessageType);
    Serial.println(")");

    const unsigned long ageMs = millis() - stats.lastFrameTime;
    Serial.printf("Frame Age:     %lu.%03lu seconds\n",
                  ageMs / 1000, ageMs % 1000);
  }

  if (stats.totalUptime > 0) {
    const float bandwidth =
        static_cast<float>(stats.bytesReceived) / (stats.totalUptime / 1000.0);
    Serial.printf("Avg Rate:      %.2f bytes/sec\n", bandwidth);

    if (stats.totalFrames > 0) {
      const float framesPerSec =
          static_cast<float>(stats.totalFrames) / (stats.totalUptime / 1000.0);
      Serial.printf("Frame Rate:    %.2f frames/sec\n", framesPerSec);
    }
  }

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

    const NtripError err = g_ntripClient.getLastError();
    Serial.print(F("Reason: "));
    Serial.println(g_ntripClient.getErrorMessage());

    switch (err) {
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

  if (millis() - lockoutStart > 120000) {
    Serial.println(F("\n🔄 Auto-resetting lockout..."));
    g_ntripClient.reset();

    String currentSettings;
    serializeJson(g_configDoc["ntrip"], currentSettings);
    updateJsonState(0, false, currentSettings);

    lockoutLogged = false;
  }
}

void configMonitorTask(void* pvParameters) {
  (void)pvParameters;

  NtripConfig currentConfig;
  bool wasInternetReachable = false;
  bool wasConfigured = false;
  unsigned long lastStatsDisplay = 0;

  for (;;) {
    const bool internetReachable = isInternetReachable();
    if (internetReachable != wasInternetReachable) {
      wasInternetReachable = internetReachable;

      if (!internetReachable) {
        Serial.println(F("[NTRIP] Internet lost - stopping client"));
        g_ntripClient.stop();
        wasConfigured = false;
      } else {
        Serial.println(F("[NTRIP] Internet restored"));
      }
    }

    if (millis() - g_lastConfigCheck > kConfigCheckInterval) {
      g_lastConfigCheck = millis();

      if (internetReachable) {
        NtripConfig newConfig;
        const bool shouldBeRunning = loadAndValidateConfig(newConfig);

        const bool configChanged =
            (newConfig.host != currentConfig.host ||
             newConfig.port != currentConfig.port ||
             newConfig.mount != currentConfig.mount ||
             newConfig.user != currentConfig.user ||
             newConfig.pass != currentConfig.pass ||
             newConfig.maxTries != currentConfig.maxTries);

        if (shouldBeRunning && (!wasConfigured || configChanged)) {
          if (configChanged && wasConfigured) {
            Serial.println(F("[NTRIP] Configuration changed - restarting client"));
          } else {
            Serial.println(F("[NTRIP] Starting NTRIP client"));
          }

          g_ntripClient.stop();
          vTaskDelay(pdMS_TO_TICKS(500));

          if (g_ntripClient.begin(newConfig, g_ntripWriter)) {
            currentConfig = newConfig;
            wasConfigured = true;

            if (!g_ntripTaskStarted) {
              g_ntripClient.startTask(0);
              g_ntripTaskStarted = true;
              Serial.println(F("[NTRIP] Task started on core 0"));
            }
          }
        } else if (!shouldBeRunning && wasConfigured) {
          Serial.println(F("[NTRIP] NTRIP disabled - stopping client"));
          g_ntripClient.stop();
          wasConfigured = false;
        }

        if (wasConfigured) {
          syncJsonWithClientState();
        }
      }
    }

    if (wasConfigured && millis() - lastStatsDisplay > 30000) {
      lastStatsDisplay = millis();

      const NtripState state = g_ntripClient.state();
      if (state == NtripState::STREAMING || state == NtripState::LOCKED_OUT) {
        displayDetailedStats();
      }
    }

    if (g_ntripClient.state() == NtripState::LOCKED_OUT) {
      handleLockout();
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void ensureConfigTemplate() {
  if (LittleFS.exists(kConfigPath)) {
    return;
  }

  Serial.println(F("[NTRIP] Creating default configuration..."));
  File file = LittleFS.open(kConfigPath, "w");
  if (!file) {
    Serial.println(F("[NTRIP] Failed to create config file!"));
    return;
  }

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
  Serial.println(F("[NTRIP] Template created"));
  Serial.println(F("[NTRIP] Edit /ntrip_config.json to configure NTRIP."));
}
}  // namespace

void ntrip_client_setup(StreamBufferHandle_t sb_ntrip2uart) {
  g_ntripWriter.setStreamBuffer(sb_ntrip2uart);

  Serial.println(F("[NTRIP] Initializing NTRIP client..."));

  if (!LittleFS.begin(true)) {
    Serial.println(F("[NTRIP] LittleFS mount failed - NTRIP disabled"));
    return;
  }

  ensureConfigTemplate();

  xTaskCreatePinnedToCore(
      configMonitorTask,
      "NtripMonitor",
      4096,
      nullptr,
      1,
      &g_ntripMonitorHandle,
      1);
  Serial.println(F("[NTRIP] Config monitor started on core 1"));
}

void ntrip_client_loop() {
  static unsigned long lastStatusPrint = 0;
  if (millis() - lastStatusPrint < 5000) {
    return;
  }
  lastStatusPrint = millis();

  const NtripState state = g_ntripClient.state();
  const bool healthy = g_ntripClient.isHealthy();
  const bool streaming = g_ntripClient.isStreaming();

  Serial.print(F("[NTRIP] "));

  switch (state) {
    case NtripState::DISCONNECTED:
      Serial.print(F("DISCONNECTED"));
      if (!isInternetReachable()) {
        Serial.print(F(" (No Internet)"));
      }
      break;
    case NtripState::CONNECTING:
      Serial.print(F("CONNECTING"));
      break;
    case NtripState::STREAMING:
      Serial.print(healthy ? F("STREAMING") : F("VALIDATING"));
      break;
    case NtripState::LOCKED_OUT:
      Serial.print(F("LOCKED_OUT"));
      break;
  }

  if (streaming) {
    const NtripStats stats = g_ntripClient.getStats();
    Serial.printf(" | ⬇ %lu KB | 📡 RTCM%d",
                  stats.bytesReceived / 1024,
                  stats.lastMessageType);

    if (stats.totalFrames > 0) {
      const unsigned long ageMs = millis() - stats.lastFrameTime;
      if (ageMs < 10000) {
        Serial.printf(" | ✓ Fresh (%.1fs ago)", ageMs / 1000.0);
      } else {
        Serial.printf(" | ⚠ Stale (%lus ago)", ageMs / 1000);
      }
    }
  }

  Serial.println();
}

#endif
