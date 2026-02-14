#include "NtripClient.h"
#include "RtcmParser.h"
#include <base64.h>
#include <stdarg.h>

#define NTRIP_LOGE(...) logf(NtripLogLevel::Error, __VA_ARGS__)
#define NTRIP_LOGW(...) logf(NtripLogLevel::Warning, __VA_ARGS__)
#define NTRIP_LOGI(...) logf(NtripLogLevel::Info, __VA_ARGS__)
#define NTRIP_LOGD(...) logf(NtripLogLevel::Debug, __VA_ARGS__)

enum class StreamPhase { VALIDATION, STREAMING };

// Local counters are flushed to shared stats at this cadence to reduce mutex contention.
static constexpr unsigned long STATS_FLUSH_MS = 250;

// ─── Config validation ──────────────────────────────────────────────────────

bool NtripClient::validateConfig(const NtripClientConfig& cfg, String& errorOut) {
  if (cfg.host.length() == 0)       { errorOut = "host is empty";            return false; }
  if (cfg.mount.length() == 0)      { errorOut = "mount is empty";           return false; }
  if (cfg.port == 0)                { errorOut = "port is zero";             return false; }
  if (cfg.bufferSize == 0)          { errorOut = "bufferSize is zero";       return false; }
  if (cfg.connectTimeoutMs == 0)    { errorOut = "connectTimeoutMs is zero"; return false; }
  if (cfg.maxTries == 0)            { errorOut = "maxTries is zero";         return false; }
  if (cfg.healthTimeoutMs == 0)     { errorOut = "healthTimeoutMs is zero";  return false; }
  return true;
}

// ─── Lifecycle ──────────────────────────────────────────────────────────────

bool NtripClient::begin(const NtripClientConfig& cfg, Print& gnss) {
  String validationError;
  if (!validateConfig(cfg, validationError)) {
    NTRIP_LOGE("Invalid config: %s", validationError.c_str());
    return false;
  }

  config = cfg;
  gnssOutput = &gnss;
  failures = 0;
  _healthy = false;
  _state = NtripState::DISCONNECTED;
  _running = false;

  // Create stats mutex once (reused across begin() calls).
  if (statsMutex == nullptr) {
    statsMutex = xSemaphoreCreateMutex();
  }
  if (statsMutex == nullptr) {
    NTRIP_LOGE("Failed to create stats mutex");
    return false;
  }

  _stats = NtripStats();
  NTRIP_LOGI("Initialized (v" NTRIP_CLIENT_VERSION ")");
  return true;
}

bool NtripClient::begin(const NtripClientConfig& cfg, HardwareSerial& gnss) {
  return begin(cfg, static_cast<Print&>(gnss));
}

// ─── Task lifecycle ─────────────────────────────────────────────────────────

#if NTRIP_CLIENT_ENABLE_TASK

bool NtripClient::startTask(uint8_t core) {
  if (_taskHandle != nullptr) {
    NTRIP_LOGW("Task already running — ignoring duplicate startTask()");
    return false;
  }

  _running = true;
  BaseType_t result = xTaskCreatePinnedToCore(
      taskEntry, "NtripClient", 8192, this, 1, &_taskHandle, core);

  if (result != pdPASS) {
    _running = false;
    _taskHandle = nullptr;
    NTRIP_LOGE("Failed to create task");
    return false;
  }

  NTRIP_LOGI("Task started on core %d", core);
  return true;
}

bool NtripClient::stopTask() {
  if (_taskHandle == nullptr) return false;

  _running = false;

  // Wait for task to self-terminate.
  unsigned long start = millis();
  while (_taskHandle != nullptr && millis() - start < 5000) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  // Force-delete if still alive after timeout.
  if (_taskHandle != nullptr) {
    vTaskDelete(_taskHandle);
    _taskHandle = nullptr;
  }

  NTRIP_LOGI("Task stopped");
  return true;
}

bool NtripClient::isTaskRunning() const {
  return _taskHandle != nullptr && _running;
}

#endif // NTRIP_CLIENT_ENABLE_TASK

// ─── Task entry and main loop ───────────────────────────────────────────────

void NtripClient::taskEntry(void* arg) {
  static_cast<NtripClient*>(arg)->taskLoop();
}

void NtripClient::taskLoop() {
  RtcmParser parser;
  StreamPhase phase = StreamPhase::VALIDATION;

  uint8_t* buffer = new uint8_t[config.bufferSize];
  if (buffer == nullptr) {
    NTRIP_LOGE("Buffer allocation failed");
#if NTRIP_CLIENT_ENABLE_TASK
    _taskHandle = nullptr;
    vTaskDelete(nullptr);
#endif
    return;
  }

  uint8_t validFrames = 0;
  unsigned long lastSampleTime = 0;
  unsigned long phaseStartTime = 0;

  // Local stats accumulators — flushed periodically to reduce mutex contention.
  uint32_t localBytes = 0;
  uint32_t localFrames = 0;
  uint32_t localCrcErrors = 0;
  uint16_t localLastMsgType = 0;
  unsigned long localLastFrameTime = 0;
  unsigned long lastStatsFlush = 0;

  while (_running) {

    // ── Close socket when not actively connected ─────────────────────────
    if (_state != NtripState::STREAMING && _state != NtripState::CONNECTING) {
      if (client.connected()) {
        client.stop();
        _healthy = false;
      }
    }

    // ── DISCONNECTED: wait for retry window, then attempt connection ─────
    if (_state == NtripState::DISCONNECTED) {
      if (millis() - lastAttempt < config.retryDelayMs) {
        vTaskDelay(pdMS_TO_TICKS(200));
        continue;
      }
      if (failures >= config.maxTries) {
        setError(NtripError::MAX_RETRIES_EXCEEDED,
                 String("Failed ") + failures + " times");
        _state = NtripState::LOCKED_OUT;
        continue;
      }
      _state = NtripState::CONNECTING;
    }

    // ── CONNECTING: TCP + HTTP handshake ─────────────────────────────────
    if (_state == NtripState::CONNECTING) {
      lastAttempt = millis();
      NTRIP_LOGI("Connecting to %s:%d/%s (attempt %d/%d)",
            config.host.c_str(), config.port, config.mount.c_str(),
            failures + 1, config.maxTries);

      if (connectCaster(config)) {
        failures = 0;
        parser.reset();
        validFrames = 0;
        phase = StreamPhase::VALIDATION;
        phaseStartTime = millis();
        lastHealth = millis();
        _healthy = false;
        _state = NtripState::STREAMING;

        // Reset local accumulators
        localBytes = 0;
        localFrames = 0;
        localCrcErrors = 0;
        localLastMsgType = 0;
        localLastFrameTime = 0;
        lastStatsFlush = millis();

        if (xSemaphoreTake(statsMutex, portMAX_DELAY)) {
          _stats.reconnects++;
          _stats.connectionStart = millis();
          _stats.lastError = NtripError::NONE;
          _stats.lastErrorMessage = "";
          xSemaphoreGive(statsMutex);
        }

        NTRIP_LOGI("Connected — validating stream");
      } else {
        failures++;
        _state = NtripState::DISCONNECTED;
      }
    }

    // ── STREAMING: read data, validate, monitor health ───────────────────
    if (_state == NtripState::STREAMING) {
      if (!client.connected()) {
        NTRIP_LOGW("Connection lost");
        setError(NtripError::TCP_CONNECT_FAILED, "Socket closed by " + config.host);
        disconnect();
        continue;
      }

      int n = client.read(buffer, config.bufferSize);
      if (n > 0) {
        localBytes += n;

        // Forward to GNSS immediately
        if (gnssOutput) {
          gnssOutput->write(buffer, n);
        }

        if (phase == StreamPhase::VALIDATION) {
          // Strict validation — parse every byte until N valid frames
          for (int i = 0; i < n; i++) {
            RtcmResult result = parser.feed(buffer[i]);

            if (result.valid) {
              validFrames++;
              lastHealth = millis();
              localFrames++;
              localLastMsgType = result.messageType;
              localLastFrameTime = millis();

              NTRIP_LOGD("Valid RTCM%d (%d/%d)",
                    result.messageType, validFrames, config.requiredValidFrames);

              if (validFrames >= config.requiredValidFrames) {
                _healthy = true;
                phase = StreamPhase::STREAMING;
                lastSampleTime = millis();
                NTRIP_LOGI("Stream validated (%lu ms)", millis() - phaseStartTime);
                break;
              }
            } else if (result.crcError) {
              localCrcErrors++;
            }
          }
        } else {
          // Passive sampling — scan for RTCM preamble periodically
          if (millis() - lastSampleTime > config.passiveSampleMs) {
            bool foundPreamble = false;
            const int scanLimit = min(n, (int)NTRIP_CLIENT_PASSIVE_SCAN_BYTES);

            for (int i = 0; i < scanLimit; i++) {
              if (buffer[i] == 0xD3) {
                foundPreamble = true;
                lastHealth = millis();
                _healthy = true;
                lastSampleTime = millis();
                localLastFrameTime = millis();
                break;
              }
            }

            if (!foundPreamble) {
              NTRIP_LOGW("No preamble in sample");
            }
          }
        }
      }

      // Zombie stream detection
      if (millis() - lastHealth > config.healthTimeoutMs) {
        NTRIP_LOGW("Zombie stream detected (%lu ms since valid data)",
                   millis() - lastHealth);
        setError(NtripError::ZOMBIE_STREAM_DETECTED,
                 "No valid RTCM for " + String(config.healthTimeoutMs / 1000) + "s");
        disconnect();
      }
    }

    // ── LOCKED_OUT: idle until user calls reset()/reconnect() ────────────
    if (_state == NtripState::LOCKED_OUT) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    // ── Periodic stats flush ─────────────────────────────────────────────
    if (millis() - lastStatsFlush >= STATS_FLUSH_MS) {
      if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10))) {
        _stats.bytesReceived += localBytes;
        _stats.totalFrames += localFrames;
        _stats.crcErrors += localCrcErrors;
        if (localLastMsgType != 0)    _stats.lastMessageType = localLastMsgType;
        if (localLastFrameTime != 0)  _stats.lastFrameTime = localLastFrameTime;
        if (_stats.connectionStart > 0) {
          _stats.totalUptime = millis() - _stats.connectionStart;
        }
        xSemaphoreGive(statsMutex);

        localBytes = 0;
        localFrames = 0;
        localCrcErrors = 0;
        localLastMsgType = 0;
        localLastFrameTime = 0;
        lastStatsFlush = millis();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  // ── Cleanup on exit ────────────────────────────────────────────────────

  // Flush remaining local stats
  if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(100))) {
    _stats.bytesReceived += localBytes;
    _stats.totalFrames += localFrames;
    _stats.crcErrors += localCrcErrors;
    xSemaphoreGive(statsMutex);
  }

  delete[] buffer;
  disconnect();

#if NTRIP_CLIENT_ENABLE_TASK
  _taskHandle = nullptr;
  vTaskDelete(nullptr);
#endif
}

// ─── Connection ─────────────────────────────────────────────────────────────

bool NtripClient::connectCaster(const NtripClientConfig& cfg) {
  NtripError err = NtripError::NONE;
  String errMsg;

  if (connectCasterWithVersion(cfg, true, err, errMsg)) {
    if (statsMutex != nullptr && xSemaphoreTake(statsMutex, portMAX_DELAY)) {
      _stats.protocolVersion = 2;
      xSemaphoreGive(statsMutex);
    }
    return true;
  }

#if NTRIP_CLIENT_ENABLE_REV1_FALLBACK
  NTRIP_LOGW("Rev2 failed, falling back to Rev1");
  if (connectCasterWithVersion(cfg, false, err, errMsg)) {
    if (statsMutex != nullptr && xSemaphoreTake(statsMutex, portMAX_DELAY)) {
      _stats.protocolVersion = 1;
      xSemaphoreGive(statsMutex);
    }
    return true;
  }
#endif

  setError(err, errMsg);
  return false;
}

bool NtripClient::connectCasterWithVersion(const NtripClientConfig& cfg,
                                           bool useRev2,
                                           NtripError& err,
                                           String& errMsg) {
  if (!client.connect(cfg.host.c_str(), cfg.port, cfg.connectTimeoutMs)) {
    err = NtripError::TCP_CONNECT_FAILED;
    errMsg = "Cannot reach " + cfg.host + ":" + cfg.port;
    return false;
  }

  String auth = base64::encode(cfg.user + ":" + cfg.pass);

  // Build NTRIP request
  client.print("GET /");
  client.print(cfg.mount);
  if (useRev2) {
    client.print(" HTTP/1.1\r\n");
  } else {
    client.print(" HTTP/1.0\r\n");
  }

  client.print("User-Agent: NTRIP ESP32 v");
  client.print(NTRIP_CLIENT_VERSION);
  client.print("\r\n");

  if (useRev2) {
    client.print("Host: ");
    client.print(cfg.host);
    client.print("\r\n");
    client.print("Ntrip-Version: Ntrip/2.0\r\n");
  }

  client.print("Authorization: Basic ");
  client.print(auth);
  client.print("\r\n");

  if (useRev2 && cfg.ggaSentence.length() > 0) {
    client.print("Ntrip-GGA: ");
    client.print(cfg.ggaSentence);
    client.print("\r\n");
  }

  client.print("\r\n");

  // Wait for response
  unsigned long start = millis();
  while (!client.available() && millis() - start < cfg.connectTimeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (!client.available()) {
    client.stop();
    err = NtripError::HTTP_TIMEOUT;
    errMsg = "No response from " + cfg.host;
    return false;
  }

  String line = client.readStringUntil('\n');
  line.trim();

  NTRIP_LOGI("Response: %s", line.c_str());

  if (line.startsWith("ICY 200") || line.startsWith("HTTP/1.1 200") ||
      line.startsWith("HTTP/1.0 200")) {
    // Drain HTTP headers before binary stream begins
    unsigned long drainStart = millis();
    while (millis() - drainStart < cfg.connectTimeoutMs) {
      if (client.available()) {
        String header = client.readStringUntil('\n');
        header.trim();
        if (header.length() == 0) {
          NTRIP_LOGI("Headers drained, binary stream starting");
          return true;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    NTRIP_LOGW("Header drain timeout, proceeding");
    return true;
  }

  // Parse specific HTTP errors
  client.stop();

  if (line.indexOf("401") >= 0) {
    err = NtripError::HTTP_AUTH_FAILED;
    errMsg = "Invalid credentials for " + cfg.host;
  } else if (line.indexOf("404") >= 0) {
    err = NtripError::HTTP_MOUNT_NOT_FOUND;
    errMsg = "Mount not found: " + cfg.mount;
  } else {
    err = NtripError::HTTP_UNKNOWN_ERROR;
    errMsg = "HTTP error: " + line;
  }

  return false;
}

// ─── State management ───────────────────────────────────────────────────────

void NtripClient::disconnect() {
  if (client.connected()) client.stop();
  _healthy = false;
  _state = NtripState::DISCONNECTED;
  if (statsMutex != nullptr && xSemaphoreTake(statsMutex, portMAX_DELAY)) {
    _stats.protocolVersion = 0;
    xSemaphoreGive(statsMutex);
  }
}

void NtripClient::setError(NtripError err, const String& msg) {
  if (statsMutex != nullptr && xSemaphoreTake(statsMutex, portMAX_DELAY)) {
    _stats.lastError = err;
    _stats.lastErrorMessage = msg;
    xSemaphoreGive(statsMutex);
  }
  NTRIP_LOGE("%s", msg.c_str());
}

bool NtripClient::isStreaming() const {
  return _state == NtripState::STREAMING;
}

bool NtripClient::isHealthy() const {
  return _healthy;
}

NtripState NtripClient::state() const {
  return _state;
}

NtripStats NtripClient::getStats() const {
  NtripStats snapshot;
  if (statsMutex != nullptr && xSemaphoreTake(statsMutex, portMAX_DELAY)) {
    snapshot = _stats;
    xSemaphoreGive(statsMutex);
  }
  return snapshot;
}

NtripError NtripClient::getLastError() const {
  NtripError err = NtripError::NONE;
  if (statsMutex != nullptr && xSemaphoreTake(statsMutex, portMAX_DELAY)) {
    err = _stats.lastError;
    xSemaphoreGive(statsMutex);
  }
  return err;
}

String NtripClient::getErrorMessage() const {
  String msg;
  if (statsMutex != nullptr && xSemaphoreTake(statsMutex, portMAX_DELAY)) {
    msg = _stats.lastErrorMessage;
    xSemaphoreGive(statsMutex);
  }
  return msg;
}

void NtripClient::stop() {
  disconnect();
  failures = config.maxTries;
  _state = NtripState::LOCKED_OUT;
  NTRIP_LOGI("Stopped");
}

void NtripClient::reset() {
  failures = 0;
  _state = NtripState::DISCONNECTED;
  if (statsMutex != nullptr && xSemaphoreTake(statsMutex, portMAX_DELAY)) {
    _stats.lastError = NtripError::NONE;
    _stats.lastErrorMessage = "";
    xSemaphoreGive(statsMutex);
  }
  NTRIP_LOGI("Reset — lockout cleared");
}

void NtripClient::reconnect() {
  disconnect();
  lastAttempt = 0;
  NTRIP_LOGI("Reconnection requested");
}

void NtripClient::setLogger(NtripLogFn logger) {
  logFn = logger;
}

void NtripClient::logf(NtripLogLevel level, const char* fmt, ...) const {
  if (logFn == nullptr || fmt == nullptr) return;

  char message[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);

  logFn(level, "NtripClient", message);
}
