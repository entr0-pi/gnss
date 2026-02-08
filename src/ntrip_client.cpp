#include "ntrip_client.h"

#if NTRIP_CLIENT_ENABLE

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include "NtripClient.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define MODULE_LOG 1
#include "logger.h"

#if WEBUI_ENABLE
#include "web_ui.h"
#endif

namespace {
const char* kNtripNvsNs = "ntrip";
const char* kNtripEnabledKey = "enabled";
const char* kNtripHostKey = "host";
const char* kNtripPortKey = "port";
const char* kNtripMountKey = "mount";
const char* kNtripUserKey = "user";
const char* kNtripPassKey = "pass";
const char* kNtripMaxTriesKey = "max_tries";
const char* kNtripRetryDelayKey = "retry_delay";
const char* kNtripHealthTimeoutKey = "health_to";
const char* kNtripPassiveMsKey = "passive_ms";
const char* kNtripReqValidKey = "req_valid";
const char* kNtripBufferSizeKey = "buf_size";
const char* kNtripConnectTimeoutKey = "conn_to";
const char* kLockoutAttemptsKey = "lock_fails";
const char* kLockoutAbandonedKey = "lock_aband";
const char* kLockoutHashKey = "lock_hash";

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

void ntripClientLogAdapter(NtripLogLevel level, const char* tag, const char* message) {
  const char* safeTag = (tag && tag[0]) ? tag : "NTRIP_LIB";
  const char* safeMsg = message ? message : "";
  switch (level) {
    case NtripLogLevel::Error:
      LOG_E(safeTag, "%s", safeMsg);
      break;
    case NtripLogLevel::Warning:
      LOG_W(safeTag, "%s", safeMsg);
      break;
    case NtripLogLevel::Info:
      LOG_I(safeTag, "%s", safeMsg);
      break;
    case NtripLogLevel::Debug:
    default:
      LOG_D(safeTag, "%s", safeMsg);
      break;
  }
}

bool loadNtripFromNvs(JsonDocument& doc, String* error) {
  Preferences prefs;
  if (!prefs.begin(kNtripNvsNs, true)) {
    if (error) *error = "NVS open failed";
    return false;
  }

  const bool hasRequired =
      prefs.isKey(kNtripEnabledKey) && prefs.isKey(kNtripHostKey) && prefs.isKey(kNtripPortKey) &&
      prefs.isKey(kNtripMountKey) && prefs.isKey(kNtripUserKey) && prefs.isKey(kNtripPassKey) &&
      prefs.isKey(kNtripMaxTriesKey) && prefs.isKey(kNtripRetryDelayKey) &&
      prefs.isKey(kNtripHealthTimeoutKey) && prefs.isKey(kNtripPassiveMsKey) &&
      prefs.isKey(kNtripReqValidKey) && prefs.isKey(kNtripBufferSizeKey) &&
      prefs.isKey(kNtripConnectTimeoutKey);

  if (!hasRequired) {
    prefs.end();
    if (error) *error = "NTRIP config not found in NVS";
    return false;
  }

  JsonObject ntrip = doc["ntrip"].to<JsonObject>();
  ntrip["enabled"] = prefs.getBool(kNtripEnabledKey);
  ntrip["host"] = prefs.getString(kNtripHostKey);
  ntrip["port"] = prefs.getUInt(kNtripPortKey);
  ntrip["mount"] = prefs.getString(kNtripMountKey);
  ntrip["user"] = prefs.getString(kNtripUserKey);
  ntrip["pass"] = prefs.getString(kNtripPassKey);
  ntrip["max_tries"] = prefs.getInt(kNtripMaxTriesKey);
  ntrip["retry_delay_ms"] = prefs.getULong(kNtripRetryDelayKey);
  ntrip["health_timeout_ms"] = prefs.getULong(kNtripHealthTimeoutKey);
  ntrip["passive_sample_ms"] = prefs.getULong(kNtripPassiveMsKey);
  ntrip["required_valid_frames"] = prefs.getUInt(kNtripReqValidKey);
  ntrip["buffer_size"] = prefs.getUInt(kNtripBufferSizeKey);
  ntrip["connect_timeout_ms"] = prefs.getULong(kNtripConnectTimeoutKey);
  prefs.end();
  return true;
}

bool loadLockoutFromNvs(JsonDocument& doc) {
  Preferences prefs;
  if (!prefs.begin(kNtripNvsNs, true)) return false;
  if (!prefs.isKey(kLockoutAttemptsKey) ||
      !prefs.isKey(kLockoutAbandonedKey) ||
      !prefs.isKey(kLockoutHashKey)) {
    prefs.end();
    return false;
  }

  JsonObject lockout = doc["lockout"].to<JsonObject>();
  lockout["failed_attempts"] = prefs.getInt(kLockoutAttemptsKey);
  lockout["abandoned"] = prefs.getBool(kLockoutAbandonedKey);
  lockout["last_config_hash"] = prefs.getString(kLockoutHashKey);
  prefs.end();
  return true;
}

bool saveLockoutToNvs(int attempts, bool abandoned, const String& currentHash) {
  Preferences prefs;
  if (!prefs.begin(kNtripNvsNs, false)) return false;
  bool ok = true;
  ok = ok && prefs.putInt(kLockoutAttemptsKey, attempts) > 0;
  ok = ok && prefs.putBool(kLockoutAbandonedKey, abandoned);
  ok = ok && prefs.putString(kLockoutHashKey, currentHash) > 0;
  prefs.end();
  return ok;
}

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
  saveLockoutToNvs(attempts, abandoned, currentHash);
}

bool loadAndValidateConfig(NtripConfig& config) {
  g_configDoc.clear();

  String nvsError;
  if (loadNtripFromNvs(g_configDoc, &nvsError)) {
    LOG_I("NTRIP", "Loaded config from NVS");
    if (!loadLockoutFromNvs(g_configDoc)) {
      const int attempts = 0;
      const bool abandoned = false;
      const String hash = "";
      g_configDoc["lockout"]["failed_attempts"] = attempts;
      g_configDoc["lockout"]["abandoned"] = abandoned;
      g_configDoc["lockout"]["last_config_hash"] = hash;
      saveLockoutToNvs(attempts, abandoned, hash);
    }
  } else {
    LOG_W("NTRIP", "NVS config missing/invalid: %s", nvsError.c_str());
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
    LOG_I("NTRIP", "New config detected. Resetting lockout.");
    attempts = 0;
    abandoned = false;
    updateJsonState(attempts, abandoned, currentSettings);
  }

  if (abandoned) {
    LOG_W("NTRIP", "Locked out due to repeated failures");
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

const char* messageName(uint16_t msgType) {
  switch (msgType) {
    case 1005: return "Station Position";
    case 1074: return "GPS MSM4";
    case 1077: return "GPS MSM7";
    case 1084: return "GLONASS MSM4";
    case 1087: return "GLONASS MSM7";
    case 1094: return "Galileo MSM4";
    case 1097: return "Galileo MSM7";
    case 1124: return "BeiDou MSM4";
    case 1127: return "BeiDou MSM7";
    case 1230: return "GLONASS Biases";
    default: return "Unknown";
  }
}

void displayDetailedStats() {
  const NtripStats stats = g_ntripClient.getStats();

  LOG_I("NTRIP", "NTRIP STATISTICS");
  LOG_I("NTRIP", "Uptime: %lu seconds", stats.totalUptime / 1000);
  LOG_I("NTRIP", "Valid Frames: %lu", stats.totalFrames);
  LOG_I("NTRIP", "CRC Errors: %lu (%.1f%%)",
        stats.crcErrors,
        stats.totalFrames > 0
            ? (100.0 * stats.crcErrors / (stats.totalFrames + stats.crcErrors))
            : 0);
  LOG_I("NTRIP", "Data RX: %.2f KB", stats.bytesReceived / 1024.0);
  LOG_I("NTRIP", "Reconnects: %lu", stats.reconnects);

  if (stats.lastMessageType > 0) {
    LOG_I("NTRIP", "Last RTCM: %d (%s)", stats.lastMessageType, messageName(stats.lastMessageType));

    const unsigned long ageMs = millis() - stats.lastFrameTime;
    LOG_I("NTRIP", "Frame Age: %lu.%03lu seconds", ageMs / 1000, ageMs % 1000);
  }

  if (stats.totalUptime > 0) {
    const float bandwidth =
        static_cast<float>(stats.bytesReceived) / (stats.totalUptime / 1000.0);
    LOG_I("NTRIP", "Avg Rate: %.2f bytes/sec", bandwidth);

    if (stats.totalFrames > 0) {
      const float framesPerSec =
          static_cast<float>(stats.totalFrames) / (stats.totalUptime / 1000.0);
      LOG_I("NTRIP", "Frame Rate: %.2f frames/sec", framesPerSec);
    }
  }

  if (stats.lastError != NtripError::NONE) {
    LOG_W("NTRIP", "Last Error: %s", stats.lastErrorMessage.c_str());
  }
}

void handleLockout() {
  static bool lockoutLogged = false;
  static unsigned long lockoutStart = 0;

  if (!lockoutLogged) {
    LOG_W("NTRIP", "CLIENT LOCKED OUT");
    LOG_W("NTRIP", "Too many connection failures.");

    const NtripError err = g_ntripClient.getLastError();
    LOG_W("NTRIP", "Reason: %s", g_ntripClient.getErrorMessage().c_str());

    switch (err) {
      case NtripError::HTTP_AUTH_FAILED:
        LOG_I("NTRIP", "Check your username and password in config file");
        LOG_I("NTRIP", "Some casters require email address as username");
        break;
      case NtripError::HTTP_MOUNT_NOT_FOUND:
        LOG_I("NTRIP", "Verify mount point name (case-sensitive)");
        LOG_I("NTRIP", "Check caster's source table");
        break;
      case NtripError::TCP_CONNECT_FAILED:
        LOG_I("NTRIP", "Check network connectivity");
        LOG_I("NTRIP", "Verify host and port are correct");
        break;
      default:
        LOG_I("NTRIP", "Update NTRIP configuration to fix this");
        LOG_I("NTRIP", "Or wait for auto-reset in 2 minutes");
    }

    lockoutLogged = true;
    lockoutStart = millis();
  }

  if (millis() - lockoutStart > 120000) {
    LOG_I("NTRIP", "Auto-resetting lockout...");
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
        LOG_W("NTRIP", "Internet lost - stopping client");
        g_ntripClient.stop();
        wasConfigured = false;
      } else {
        LOG_I("NTRIP", "Internet restored");
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
            LOG_I("NTRIP", "Configuration changed - restarting client");
          } else {
            LOG_I("NTRIP", "Starting NTRIP client");
          }

          g_ntripClient.stop();
          vTaskDelay(pdMS_TO_TICKS(500));

          if (g_ntripClient.begin(newConfig, g_ntripWriter)) {
            currentConfig = newConfig;
            wasConfigured = true;

            if (!g_ntripTaskStarted) {
              g_ntripClient.startTask(0);
              g_ntripTaskStarted = true;
              LOG_I("NTRIP", "Task started on core 0");
            }
          }
        } else if (!shouldBeRunning && wasConfigured) {
          LOG_I("NTRIP", "NTRIP disabled - stopping client");
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

}  // namespace

void ntrip_client_setup(StreamBufferHandle_t sb_ntrip2uart) {
  g_ntripWriter.setStreamBuffer(sb_ntrip2uart);
  g_ntripClient.setLogger(ntripClientLogAdapter);

  LOG_I("NTRIP", "Initializing NTRIP client...");

  xTaskCreatePinnedToCore(
      configMonitorTask,
      "NtripMonitor",
      4096,
      nullptr,
      1,
      &g_ntripMonitorHandle,
      1);
  LOG_I("NTRIP", "Config monitor started on core 1");
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

  switch (state) {
    case NtripState::DISCONNECTED:
      if (!isInternetReachable()) {
        LOG_I("NTRIP", "DISCONNECTED (No Internet)");
      } else {
        LOG_I("NTRIP", "DISCONNECTED");
      }
      break;
    case NtripState::CONNECTING:
      LOG_I("NTRIP", "CONNECTING");
      break;
    case NtripState::STREAMING:
      LOG_I("NTRIP", "%s", healthy ? "STREAMING" : "VALIDATING");
      break;
    case NtripState::LOCKED_OUT:
      LOG_W("NTRIP", "LOCKED_OUT");
      break;
  }

  if (streaming) {
    const NtripStats stats = g_ntripClient.getStats();
    LOG_I("NTRIP", "Stream: %lu KB | RTCM%d", stats.bytesReceived / 1024, stats.lastMessageType);

    if (stats.totalFrames > 0) {
      const unsigned long ageMs = millis() - stats.lastFrameTime;
      if (ageMs < 10000) {
        LOG_I("NTRIP", "Fresh (%.1fs ago)", ageMs / 1000.0);
      } else {
        LOG_W("NTRIP", "Stale (%lus ago)", ageMs / 1000);
      }
    }
  }
}

bool ntrip_client_get_snapshot(NtripClientSnapshot& out) {
  const NtripState state = g_ntripClient.state();
  const NtripStats stats = g_ntripClient.getStats();

  out.connected = state == NtripState::STREAMING || state == NtripState::CONNECTING;
  out.healthy = g_ntripClient.isHealthy();
  out.streaming = g_ntripClient.isStreaming();
  out.bytesReceived = static_cast<uint32_t>(stats.bytesReceived);
  out.totalFrames = static_cast<uint32_t>(stats.totalFrames);
  out.lastMessageType = stats.lastMessageType;
  out.lastFrameAgeMs = (stats.lastFrameTime > 0) ? static_cast<uint32_t>(millis() - stats.lastFrameTime) : 0;
  return true;
}

#endif
