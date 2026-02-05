#include "NtripClient.h"
#include "RtcmParser.h"
#include <base64.h>

// Implementation notes:
// - The task loop runs continuously on a FreeRTOS task.
// - Two-phase validation is used:
//   1) Strict validation: parse every byte and require N valid frames.
//   2) Passive sampling: periodically scan for RTCM preamble to detect stalls.
// - Health and stats are protected by mutexes for safe cross-task access.

enum class StreamPhase { VALIDATION, STREAMING };

bool NtripClient::begin(const NtripConfig& cfg, HardwareSerial& gnss) {
  // Store configuration and GNSS serial reference, reset state, init mutexes.
  config = cfg;
  gnssSerial = &gnss;
  failures = 0;
  _healthy = false;
  _state = NtripState::DISCONNECTED;
  
  // Create mutexes for thread-safe access
  if (statsMutex == nullptr) {
    statsMutex = xSemaphoreCreateMutex();
  }
  if (configMutex == nullptr) {
    configMutex = xSemaphoreCreateMutex();
  }
  
  // Reset statistics
  _stats = NtripStats();
  
  Serial.println(F("[NtripClient] Initialized"));
  return true;
}

void NtripClient::startTask(uint8_t core) {
  // Start FreeRTOS task pinned to the requested core.
  xTaskCreatePinnedToCore(taskEntry, "NtripClient", 8192, this, 1, nullptr, core);
  Serial.printf("[NtripClient] Task started on core %d\n", core);
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
  if (xSemaphoreTake(configMutex, portMAX_DELAY)) {
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
      if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100))) {
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
      Serial.printf("[NtripClient] Connecting to %s:%d/%s (attempt %d/%d)\n",
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
        
        if (xSemaphoreTake(statsMutex, portMAX_DELAY)) {
          _stats.reconnects++;
          _stats.connectionStart = millis();
          _stats.lastError = NtripError::NONE;
          _stats.lastErrorMessage = "";
          xSemaphoreGive(statsMutex);
        }
        
        Serial.println(F("[NtripClient] Connected - validating stream..."));
      } else {
        failures++;
        _state = NtripState::DISCONNECTED;
      }
    }
    
    if (_state == NtripState::STREAMING) {
      // Stream handling and health checks.
      if (!client.connected()) {
        Serial.println(F("[NtripClient] Connection lost"));
        setError(NtripError::TCP_CONNECT_FAILED, "Socket closed by server");
        disconnect();
        continue;
      }
      
      int n = client.read(buffer, localConfig.bufferSize);
      if (n > 0) {
        
        // Update statistics
        if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10))) {
          _stats.bytesReceived += n;
          xSemaphoreGive(statsMutex);
        }
        
        // FAST PATH: Write to GNSS immediately
        gnssSerial->write(buffer, n);
        
        if (phase == StreamPhase::VALIDATION) {
          // Strict validation of RTCM frames until required count is reached.
          // PHASE 1: Strict validation - parse every byte
          for (int i = 0; i < n; i++) {
            RtcmResult result = parser.feed(buffer[i]);
            
            if (result.valid) {
              validFrames++;
              lastHealth = millis();
              
              // Update statistics
              if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10))) {
                _stats.totalFrames++;
                _stats.lastMessageType = result.messageType;
                _stats.lastFrameTime = millis();
                xSemaphoreGive(statsMutex);
              }
              
              Serial.printf("[NtripClient] Valid RTCM%d frame (%d/%d)\n",
                           result.messageType, validFrames, localConfig.requiredValidFrames);

              if (validFrames >= localConfig.requiredValidFrames) {
                _healthy = true;
                phase = StreamPhase::STREAMING;
                lastSampleTime = millis();
                Serial.printf("[NtripClient] Stream validated! (%lu ms)\n", 
                             millis() - phaseStartTime);
                break;
              }
            } else if (result.crcError) {
              if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10))) {
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

                if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10))) {
                  _stats.lastFrameTime = millis();
                  xSemaphoreGive(statsMutex);
                }
                break;
              }
            }

            if (!foundPreamble) {
              Serial.println(F("[NtripClient] Warning: No preamble in sample"));
            }
          }
        }
      }
      
      // ZOMBIE STREAM DETECTION
      if (millis() - lastHealth > localConfig.healthTimeoutMs) {
        Serial.printf("[NtripClient] Zombie stream detected (%lu ms since valid data)\n",
                     millis() - lastHealth);
        setError(NtripError::ZOMBIE_STREAM_DETECTED,
                 "No valid RTCM for " + String(localConfig.healthTimeoutMs / 1000) + "s");
        disconnect();
      }
      
      // Update total uptime
      if (xSemaphoreTake(statsMutex, pdMS_TO_TICKS(10))) {
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
  // Open TCP connection and send NTRIP HTTP request with Basic auth.
  if (!client.connect(cfg.host.c_str(), cfg.port, cfg.connectTimeoutMs)) {
    setError(NtripError::TCP_CONNECT_FAILED,
             "Cannot reach " + cfg.host + ":" + cfg.port);
    return false;
  }

  String auth = base64::encode(cfg.user + ":" + cfg.pass);

  client.print("GET /");
  client.print(cfg.mount);
  client.print(" HTTP/1.0\r\n");
  client.print("User-Agent: NTRIP ESP32 v");
  client.print(NTRIP_CLIENT_VERSION);
  client.print("\r\n");
  client.print("Authorization: Basic ");
  client.print(auth);
  client.print("\r\n\r\n");

  unsigned long start = millis();
  while (!client.available() && millis() - start < cfg.connectTimeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (!client.available()) {
    client.stop();
    setError(NtripError::HTTP_TIMEOUT, "No response from caster");
    return false;
  }

  String line = client.readStringUntil('\n');
  line.trim();

  Serial.printf("[NtripClient] Server response: %s\n", line.c_str());

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
          Serial.println(F("[NtripClient] Headers drained, starting binary stream"));
          return true;
        }
        // Optional: log headers for debugging
        // Serial.printf("[NtripClient] Header: %s\n", header.c_str());
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    // Timeout waiting for header end - proceed anyway but warn
    Serial.println(F("[NtripClient] Warning: Header drain timeout, proceeding"));
    return true;
  }

  // Parse specific HTTP errors
  client.stop();

  if (line.indexOf("401") >= 0) {
    setError(NtripError::HTTP_AUTH_FAILED, "Invalid username/password");
  } else if (line.indexOf("404") >= 0) {
    setError(NtripError::HTTP_MOUNT_NOT_FOUND, "Mount point not found: " + cfg.mount);
  } else {
    setError(NtripError::HTTP_UNKNOWN_ERROR, "HTTP error: " + line);
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
  if (xSemaphoreTake(statsMutex, portMAX_DELAY)) {
    _stats.lastError = err;
    _stats.lastErrorMessage = msg;
    xSemaphoreGive(statsMutex);
  }
  Serial.printf("[NtripClient] ERROR: %s\n", msg.c_str());
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
  if (xSemaphoreTake(statsMutex, portMAX_DELAY)) {
    stats = _stats;
    xSemaphoreGive(statsMutex);
  }
  return stats;
}

NtripError NtripClient::getLastError() const {
  // Return last error code.
  NtripError err = NtripError::NONE;
  if (xSemaphoreTake(statsMutex, portMAX_DELAY)) {
    err = _stats.lastError;
    xSemaphoreGive(statsMutex);
  }
  return err;
}

String NtripClient::getErrorMessage() const {
  // Return last error message.
  String msg;
  if (xSemaphoreTake(statsMutex, portMAX_DELAY)) {
    msg = _stats.lastErrorMessage;
    xSemaphoreGive(statsMutex);
  }
  return msg;
}

void NtripClient::stop() {
  // Force lockout by setting failures to maxTries.
  disconnect();
  if (xSemaphoreTake(configMutex, portMAX_DELAY)) {
    failures = config.maxTries;
    xSemaphoreGive(configMutex);
  }
  _state = NtripState::LOCKED_OUT;
  Serial.println(F("[NtripClient] Stopped by user"));
}

void NtripClient::reset() {
  // Clear lockout and error status; reconnect will happen on next loop.
  failures = 0;
  _state = NtripState::DISCONNECTED;
  
  if (xSemaphoreTake(statsMutex, portMAX_DELAY)) {
    _stats.lastError = NtripError::NONE;
    _stats.lastErrorMessage = "";
    xSemaphoreGive(statsMutex);
  }
  
  Serial.println(F("[NtripClient] Reset - lockout cleared"));
}

void NtripClient::reconnect() {
  // Force immediate retry by clearing lastAttempt.
  disconnect();
  lastAttempt = 0;  // Force immediate retry
  Serial.println(F("[NtripClient] Reconnection requested"));
}
