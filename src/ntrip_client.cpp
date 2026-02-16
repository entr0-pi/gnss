#include "ntrip_client.h"

#if NTRIP_CLIENT_ENABLE

#include <Arduino.h>
#include <WiFi.h>
#include "NtripClient.h"
#include "config_ntrip.h"
#if NMEA_ENABLE
#include "parsing_nmea.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define MODULE_LOG 1
#include "logger.h"

#include "internet_probe.h"

namespace {
NtripClient g_ntripClient;
NtripConfig g_ntripCfg;
NtripLockout g_lockout;
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

String configHash(const NtripConfig& cfg) {
  return cfg.host + ":" + String(cfg.port) + "/" + cfg.mount + "@" + cfg.user
       + ":" + cfg.pass + "|" + String(cfg.max_tries) + "|" + String(cfg.retry_delay_ms)
       + "|" + String(cfg.health_timeout_ms) + "|" + String(cfg.passive_sample_ms)
       + "|" + String(cfg.required_valid_frames) + "|" + String(cfg.buffer_size)
       + "|" + String(cfg.connect_timeout_ms) + "|" + String(cfg.send_gga ? 1 : 0);
}

bool isInternetReachable() {
  return internet_probe_is_reachable();
}

void saveLockout(int attempts, bool abandoned, const String& hash) {
  if (g_lockout.failed_attempts == attempts &&
      g_lockout.abandoned == abandoned &&
      g_lockout.last_config_hash == hash) {
    return;
  }

  g_lockout.failed_attempts = attempts;
  g_lockout.abandoned = abandoned;
  g_lockout.last_config_hash = hash;
  ntrip_config_save(g_ntripCfg, &g_lockout, nullptr);
}

bool loadAndValidateConfig(NtripClientConfig& config) {
  String nvsError;
  if (!ntrip_config_load(g_ntripCfg, &g_lockout, &nvsError)) {
    LOG_W("NTRIP", "NVS config missing/invalid: %s", nvsError.c_str());
    return false;
  }
  LOG_I("NTRIP", "Loaded config from NVS");

  if (!g_ntripCfg.enabled) {
    return false;
  }

  const String currentHash_ = configHash(g_ntripCfg);
  if (currentHash_ != g_lockout.last_config_hash) {
    LOG_I("NTRIP", "New config detected. Resetting lockout.");
    saveLockout(0, false, currentHash_);
  }

  if (g_lockout.abandoned) {
    LOG_W("NTRIP", "Locked out due to repeated failures");
    return false;
  }

  config.host               = g_ntripCfg.host;
  config.port               = g_ntripCfg.port;
  config.mount              = g_ntripCfg.mount;
  config.user               = g_ntripCfg.user;
  config.pass               = g_ntripCfg.pass;
  config.maxTries            = g_ntripCfg.max_tries;
  config.retryDelayMs        = g_ntripCfg.retry_delay_ms;
  config.healthTimeoutMs     = g_ntripCfg.health_timeout_ms;
  config.passiveSampleMs     = g_ntripCfg.passive_sample_ms;
  config.requiredValidFrames = g_ntripCfg.required_valid_frames;
  config.bufferSize          = g_ntripCfg.buffer_size;
  config.connectTimeoutMs    = g_ntripCfg.connect_timeout_ms;

  // Wire GGA sentence if enabled
#if NMEA_ENABLE
  if (g_ntripCfg.send_gga) {
    String lastGga;
    if (nmea_get_last_gga(lastGga) && lastGga.length() > 0) {
      config.ggaSentence = lastGga;
      LOG_D("NTRIP", "GGA enabled and available: %s", lastGga.c_str());
    } else {
      config.ggaSentence = "";
      LOG_W("NTRIP", "GGA enabled but no valid GGA sentence available");
    }
  } else {
    config.ggaSentence = "";
  }
#else
  // NMEA not enabled - GGA cannot be provided
  config.ggaSentence = "";
  if (g_ntripCfg.send_gga) {
    LOG_W("NTRIP", "GGA enabled but NMEA not compiled in");
  }
#endif

  return true;
}

void syncLockoutWithClientState() {
  const NtripState state = g_ntripClient.state();
  const String hash = configHash(g_ntripCfg);

  if (state == NtripState::STREAMING && g_ntripClient.isHealthy()) {
    if (g_lockout.failed_attempts != 0 || g_lockout.abandoned) {
      saveLockout(0, false, hash);
    }
  } else if (state == NtripState::LOCKED_OUT) {
    if (!g_lockout.abandoned) {
      saveLockout(g_ntripCfg.max_tries, true, hash);
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
    saveLockout(0, false, configHash(g_ntripCfg));

    lockoutLogged = false;
  }
}

void configMonitorTask(void* pvParameters) {
  (void)pvParameters;

  NtripClientConfig currentConfig;
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
        NtripClientConfig newConfig;
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
#if NTRIP_CLIENT_ENABLE_TASK
              g_ntripClient.startTask(0);
              g_ntripTaskStarted = true;
              LOG_I("NTRIP", "Task started on core 0");
#else
              // Task mode disabled at compile time; keep monitor alive but do not
              // call startTask()/stopTask() APIs that are not compiled in.
              g_ntripTaskStarted = true;
              LOG_W("NTRIP", "NtripClient task mode disabled (NTRIP_CLIENT_ENABLE_TASK=0)");
#endif
            }
          }
        } else if (!shouldBeRunning && wasConfigured) {
          LOG_I("NTRIP", "NTRIP disabled - stopping client");
          g_ntripClient.stop();
          wasConfigured = false;
        }

        if (wasConfigured) {
          syncLockoutWithClientState();
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
  out.protocolVersion = stats.protocolVersion;
  return true;
}

#endif
