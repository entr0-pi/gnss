#include "NtripClient.h"
#include "RtcmParser.h"
#include <base64.h>
#include <stdarg.h>

#define NTRIP_LOGE(...) logf(NtripLogLevel::Error, __VA_ARGS__)
#define NTRIP_LOGW(...) logf(NtripLogLevel::Warning, __VA_ARGS__)
#define NTRIP_LOGI(...) logf(NtripLogLevel::Info, __VA_ARGS__)
#define NTRIP_LOGD(...) logf(NtripLogLevel::Debug, __VA_ARGS__)

// Implementation notes:
// - The task loop runs continuously on a FreeRTOS task.
// - Two-phase validation is used:
//   1) Strict validation: parse every byte and require N valid frames.
//   2) Passive sampling: periodically scan for RTCM preamble to detect stalls.
// - Health and stats are protected by mutexes for safe cross-task access.

enum class StreamPhase { VALIDATION, STREAMING };

bool NtripClient::ensureMutexes() const {
  if (statsMutex == nullptr) {
    statsMutex = xSemaphoreCreateMutex();
  }
  if (configMutex == nullptr) {
    configMutex = xSemaphoreCreateMutex();
  }
  if (statsMutex == nullptr || configMutex == nullptr) {
    NTRIP_LOGE("failed to create mutexes");
    return false;
  }
  return true;
}

bool NtripClient::begin(const NtripConfig& cfg, Print& gnss) {
  // Store configuration and GNSS serial reference, reset state, init mutexes.
  config = cfg;
  gnssOutput = &gnss;
  failures = 0;
  _healthy = false;
  _state = NtripState::DISCONNECTED;

  if (!ensureMutexes()) {
    return false;
  }

  // Reset statistics
  _stats = NtripStats();

  NTRIP_LOGI("Initialized");
  return true;
}

bool NtripClient::begin(const NtripConfig& cfg, HardwareSerial& gnss) {
  return begin(cfg, static_cast<Print&>(gnss));
}

void NtripClient::startTask(uint8_t core) {
  // Start FreeRTOS task pinned to the requested core.
  xTaskCreatePinnedToCore(taskEntry, "NtripClient", 8192, this, 1, nullptr, core);
  NTRIP_LOGI("Task started on core %d", core);
}

void NtripClient::taskEntry(void* arg) {
  // Static trampoline used by FreeRTOS.
  static_cast<NtripClient*>(arg)->taskLoop();
}

void NtripClient::taskLoop() {
  // Main state machine: connect, validate, stream, monitor, and recover.
  RtcmParser parser;
  StreamPhase phase = StreamPhase::VALIDATION;

  // Thread-safe config snapshot - copied at connection boundaries
  NtripConfig localConfig;

  // Copy initial config under mutex protection
  if (ensureMutexes() && xSemaphoreTake(configMutex, portMAX_DELAY)) {
    localConfig = config;
    xSemaphoreGive(configMutex);
  }

  uint8_t* buffer = new uint8_t[localConfig.bufferSize];
  uint8_t validFrames = 0;
  unsigned long lastSampleTime = 0;
  unsigned long phaseStartTime = 0;

  for (;;) {

    if (_state == NtripState::DISCONNECTED) {
      // Safe point to refresh config and apply new tuning parameters.
      // Take fresh config snapshot when disconnected (safe boundary)
      if (ensureMutexes() && xSemaphoreTake(configMutex, pdMS_TO_TICKS(100))) {
        localConfig = config;
        xSemaphoreGive(configMutex);
      }

      if (millis() - lastAttempt < localConfig.retryDelayMs) {
        vTaskDelay(pdMS_TO_TICKS(200));
        continue;
      }
      if (failures >= localConfig.maxTries) {
        // Lock out after exceeding max tries; user must reset/reconnect.
        setError(NtripError::MAX_RETRIES_EXCEEDED,
                 String("Failed ") + failures + " times");
        _state = NtripState::LOCKED_OUT;
        continue;
      }
      _state = NtripState::CONNECTING;
    }

    if (_state == NtripState::CONNECTING) {
      // Attempt TCP + HTTP connection to the caster.
      lastAttempt = millis();
      NTRIP_LOGI("Connecting to %s:%d/%s (attempt %d/%d)",
            localConfig.host.c_str(), localConfig.port, localConfig.mount.c_str(),
            failures + 1, localConfig.maxTries);
      
      if (connectCaster(localConfig)) {
        // Connection established; enter validation phase.
        failures = 0;
        parser.reset();
        validFrames = 0;
        phase = StreamPhase::VALIDATION;
        phaseStartTime = millis();
        lastHealth = millis();
        _healthy = false;
        _state = NtripState::STREAMING;
        
        if (ensureMutexes() && xSemaphoreTake(statsMutex, portMAX_DELAY)) {
          _stats.reconnects++;
          _stats.connectionStart = millis();
          _stats.lastError = NtripError::NONE;
          _stats.lastErrorMessage = "";
          xSemaphoreGive(statsMutex);
        }
        
        NTRIP_LOGI("Connected - validating stream...");
      } else {
        failures++;
        _state = NtripState::DISCONNECTED;
      }
    }
    
    if (_state == NtripState::STREAMING) {
      // Stream handling and health checks.
      if (!client.connected()) {
        NTRIP_LOGW("Connection lost");
        setError(NtripError::TCP_CONNECT_FAILED, "Socket closed by server");
        disconnect();
        continue;
      }
      
      int n = client.read(buffer, localConfig.bufferSize);
      if (n > 0) {
        
        // Update statistics
        if (ensureMutexes() && xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10))) {
          _stats.bytesReceived += n;
          xSemaphoreGive(statsMutex);
        }
        
        // FAST PATH: Write to GNSS immediately
        if (gnssOutput) {
          gnssOutput->write(buffer, n);
        }
        
        if (phase == StreamPhase::VALIDATION) {
          // Strict validation of RTCM frames until required count is reached.
          // PHASE 1: Strict validation - parse every byte
          for (int i = 0; i < n; i++) {
            RtcmResult result = parser.feed(buffer[i]);
            
            if (result.valid) {
              validFrames++;
              lastHealth = millis();
              
              // Update statistics
              if (ensureMutexes() && xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10))) {
                _stats.totalFrames++;
                _stats.lastMessageType = result.messageType;
                _stats.lastFrameTime = millis();
                xSemaphoreGive(statsMutex);
              }
              
              NTRIP_LOGD("Valid RTCM%d frame (%d/%d)",
                    result.messageType, validFrames, localConfig.requiredValidFrames);

              if (validFrames >= localConfig.requiredValidFrames) {
                _healthy = true;
                phase = StreamPhase::STREAMING;
                lastSampleTime = millis();
                NTRIP_LOGI("Stream validated! (%lu ms)", millis() - phaseStartTime);
                break;
              }
            } else if (result.crcError) {
              if (ensureMutexes() && xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10))) {
                _stats.crcErrors++;
                xSemaphoreGive(statsMutex);
              }
            }
          }
        } else {
          // Passive sampling to detect "zombie" streams without full parsing.
          // PHASE 2: Passive sampling - check buffer periodically for RTCM preamble
          if (millis() - lastSampleTime > localConfig.passiveSampleMs) {
            bool foundPreamble = false;

            // Scan up to first 128 bytes for RTCM preamble (0xD3)
            // TCP segmentation is arbitrary, so preamble may not be at buffer start
            const int scanLimit = min(n, 128);
            for (int i = 0; i < scanLimit; i++) {
              if (buffer[i] == 0xD3) {
                foundPreamble = true;
                lastHealth = millis();
                _healthy = true;
                lastSampleTime = millis();

                if (ensureMutexes() && xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10))) {
                  _stats.lastFrameTime = millis();
                  xSemaphoreGive(statsMutex);
                }
                break;
              }
            }

            if (!foundPreamble) {
              NTRIP_LOGW("No preamble in sample");
            }
          }
        }
      }
      
      // ZOMBIE STREAM DETECTION
      if (millis() - lastHealth > localConfig.healthTimeoutMs) {
        NTRIP_LOGW("Zombie stream detected (%lu ms since valid data)", millis() - lastHealth);
        setError(NtripError::ZOMBIE_STREAM_DETECTED,
                 "No valid RTCM for " + String(localConfig.healthTimeoutMs / 1000) + "s");
        disconnect();
      }
      
      // Update total uptime
      if (ensureMutexes() && xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10))) {
        if (_stats.connectionStart > 0) {
          _stats.totalUptime = millis() - _stats.connectionStart;
        }
        xSemaphoreGive(statsMutex);
      }
    }
    
    if (_state == NtripState::LOCKED_OUT) {
      // Stay idle until user calls reset()/reconnect().
      disconnect();
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
    
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  
  delete[] buffer;
}

bool NtripClient::connectCaster(const NtripConfig& cfg) {
  NtripError err = NtripError::NONE;
  String errMsg;

  if (connectCasterWithVersion(cfg, true, err, errMsg)) {
    return true;
  }

  NTRIP_LOGW("NTRIP Rev2 failed, falling back to Rev1");

  if (connectCasterWithVersion(cfg, false, err, errMsg)) {
    return true;
  }

  setError(err, errMsg);
  return false;
}

bool NtripClient::connectCasterWithVersion(const NtripConfig& cfg,
                                           bool useRev2,
                                           NtripError& err,
                                           String& errMsg) {
  // Open TCP connection and send NTRIP HTTP request with Basic auth.
  if (!client.connect(cfg.host.c_str(), cfg.port, cfg.connectTimeoutMs)) {
    err = NtripError::TCP_CONNECT_FAILED;
    errMsg = "Cannot reach " + cfg.host + ":" + cfg.port;
    return false;
  }

  String auth = base64::encode(cfg.user + ":" + cfg.pass);

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
    client.print("Connection: close\r\n");
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

  unsigned long start = millis();
  while (!client.available() && millis() - start < cfg.connectTimeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (!client.available()) {
    client.stop();
    err = NtripError::HTTP_TIMEOUT;
    errMsg = "No response from caster";
    return false;
  }

  String line = client.readStringUntil('\n');
  line.trim();

  NTRIP_LOGI("Server response: %s", line.c_str());

  if (line.startsWith("ICY 200") || line.startsWith("HTTP/1.1 200") ||
      line.startsWith("HTTP/1.0 200")) {
    // Drain headers to avoid forwarding ASCII to GNSS.
    // Drain remaining HTTP headers until empty line (header/body separator)
    // This prevents ASCII header bytes from being forwarded to the GNSS receiver
    unsigned long drainStart = millis();
    while (millis() - drainStart < cfg.connectTimeoutMs) {
      if (client.available()) {
        String header = client.readStringUntil('\n');
        header.trim();
        if (header.length() == 0) {
          // Empty line found - headers complete, binary stream begins
          NTRIP_LOGI("Headers drained, starting binary stream");
          return true;
        }
        // Optional: log headers for debugging
        // Serial.printf("[NtripClient] Header: %s\n", header.c_str());
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    // Timeout waiting for header end - proceed anyway but warn
    NTRIP_LOGW("Header drain timeout, proceeding");
    return true;
  }

  // Parse specific HTTP errors
  client.stop();

  if (line.indexOf("401") >= 0) {
    err = NtripError::HTTP_AUTH_FAILED;
    errMsg = "Invalid username/password";
  } else if (line.indexOf("404") >= 0) {
    err = NtripError::HTTP_MOUNT_NOT_FOUND;
    errMsg = "Mount point not found: " + cfg.mount;
  } else {
    err = NtripError::HTTP_UNKNOWN_ERROR;
    errMsg = "HTTP error: " + line;
  }

  return false;
}

void NtripClient::disconnect() {
  // Close socket and reset health/state.
  if (client.connected()) client.stop();
  _healthy = false;
  _state = NtripState::DISCONNECTED;
}

void NtripClient::setError(NtripError err, const String& msg) {
  // Record error in stats for external inspection.
  if (ensureMutexes() && xSemaphoreTake(statsMutex, portMAX_DELAY)) {
    _stats.lastError = err;
    _stats.lastErrorMessage = msg;
    xSemaphoreGive(statsMutex);
  }
  NTRIP_LOGE("%s", msg.c_str());
}

bool NtripClient::isStreaming() const { 
  // True when actively streaming (may still be unhealthy).
  return _state == NtripState::STREAMING; 
}

bool NtripClient::isHealthy() const { 
  // True when validation has passed and recent data is flowing.
  return _healthy; 
}

NtripState NtripClient::state() const { 
  // Current connection state.
  return _state; 
}

NtripStats NtripClient::getStats() const {
  // Snapshot stats under mutex.
  NtripStats stats;
  if (ensureMutexes() && xSemaphoreTake(statsMutex, portMAX_DELAY)) {
    stats = _stats;
    xSemaphoreGive(statsMutex);
  } else {
    stats = _stats;
  }
  return stats;
}

NtripError NtripClient::getLastError() const {
  // Return last error code.
  NtripError err = NtripError::NONE;
  if (ensureMutexes() && xSemaphoreTake(statsMutex, portMAX_DELAY)) {
    err = _stats.lastError;
    xSemaphoreGive(statsMutex);
  } else {
    err = _stats.lastError;
  }
  return err;
}

String NtripClient::getErrorMessage() const {
  // Return last error message.
  String msg;
  if (ensureMutexes() && xSemaphoreTake(statsMutex, portMAX_DELAY)) {
    msg = _stats.lastErrorMessage;
    xSemaphoreGive(statsMutex);
  } else {
    msg = _stats.lastErrorMessage;
  }
  return msg;
}

void NtripClient::stop() {
  // Force lockout by setting failures to maxTries.
  disconnect();
  if (ensureMutexes() && xSemaphoreTake(configMutex, portMAX_DELAY)) {
    failures = config.maxTries;
    xSemaphoreGive(configMutex);
  }
  _state = NtripState::LOCKED_OUT;
  NTRIP_LOGI("Stopped by user");
}

void NtripClient::reset() {
  // Clear lockout and error status; reconnect will happen on next loop.
  failures = 0;
  _state = NtripState::DISCONNECTED;
  
  if (ensureMutexes() && xSemaphoreTake(statsMutex, portMAX_DELAY)) {
    _stats.lastError = NtripError::NONE;
    _stats.lastErrorMessage = "";
    xSemaphoreGive(statsMutex);
  }
  
  NTRIP_LOGI("Reset - lockout cleared");
}

void NtripClient::reconnect() {
  // Force immediate retry by clearing lastAttempt.
  disconnect();
  lastAttempt = 0;  // Force immediate retry
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
