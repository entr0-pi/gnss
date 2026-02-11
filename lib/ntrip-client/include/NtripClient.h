#pragma once

// ─── Compile-time feature flags ─────────────────────────────────────────────
// Override via build_flags (e.g. -DNTRIP_CLIENT_ENABLE_TASK=0).

#ifndef NTRIP_CLIENT_ENABLE_TASK
#define NTRIP_CLIENT_ENABLE_TASK 1
#endif

#ifndef NTRIP_CLIENT_ENABLE_REV1_FALLBACK
#define NTRIP_CLIENT_ENABLE_REV1_FALLBACK 1
#endif

#ifndef NTRIP_CLIENT_PASSIVE_SCAN_BYTES
#define NTRIP_CLIENT_PASSIVE_SCAN_BYTES 128
#endif

// Platform gate: task mode requires FreeRTOS
#if NTRIP_CLIENT_ENABLE_TASK
  #if !defined(ESP_PLATFORM) && !defined(ARDUINO_ARCH_ESP32)
    #error "NTRIP_CLIENT_ENABLE_TASK=1 requires FreeRTOS (ESP32). Set to 0 for non-RTOS targets."
  #endif
#endif

#include <WiFiClient.h>
#include <Arduino.h>

#define NTRIP_CLIENT_VERSION "2.1.0"

// ─── Log levels ─────────────────────────────────────────────────────────────

enum class NtripLogLevel : uint8_t {
  Error = 1,
  Warning = 2,
  Info = 3,
  Debug = 4,
};

using NtripLogFn = void (*)(NtripLogLevel level, const char* tag, const char* message);

// ─── Configuration ──────────────────────────────────────────────────────────

struct NtripConfig {
  String host;
  uint16_t port = 2101;
  String mount;
  String user;
  String pass;
  String ggaSentence;                // Optional GGA sent as Ntrip-GGA header (Rev2)
  uint8_t maxTries = 5;             // Reconnect attempts before lockout
  uint32_t retryDelayMs = 30000;    // Delay between attempts
  uint32_t healthTimeoutMs = 60000; // Zombie stream detection timeout
  uint32_t passiveSampleMs = 5000;  // Passive health check interval
  uint8_t requiredValidFrames = 3;  // Frames needed for stream validation
  uint16_t bufferSize = 1024;       // TCP read buffer size
  uint32_t connectTimeoutMs = 5000; // TCP + HTTP response timeout
};

// ─── States and errors ──────────────────────────────────────────────────────

enum class NtripState {
  DISCONNECTED,
  CONNECTING,
  STREAMING,
  LOCKED_OUT
};

enum class NtripError {
  NONE = 0,
  INVALID_CONFIG,
  TCP_CONNECT_FAILED,
  HTTP_AUTH_FAILED,
  HTTP_MOUNT_NOT_FOUND,
  HTTP_TIMEOUT,
  HTTP_UNKNOWN_ERROR,
  STREAM_VALIDATION_FAILED,
  ZOMBIE_STREAM_DETECTED,
  MAX_RETRIES_EXCEEDED
};

// ─── Statistics ─────────────────────────────────────────────────────────────

struct NtripStats {
  uint32_t totalFrames = 0;
  uint32_t crcErrors = 0;
  uint32_t bytesReceived = 0;
  uint32_t reconnects = 0;
  uint32_t totalUptime = 0;
  uint16_t lastMessageType = 0;
  unsigned long lastFrameTime = 0;
  unsigned long connectionStart = 0;
  NtripError lastError = NtripError::NONE;
  String lastErrorMessage;
};

// ─── NtripClient ────────────────────────────────────────────────────────────
//
// Thread-safety contract
// ~~~~~~~~~~~~~~~~~~~~~~
// - begin() and startTask()/stopTask() must be called from the same context
//   (typically Arduino setup/loop). Never call begin() while the task runs.
// - Query methods — state(), isStreaming(), isHealthy(), getStats(),
//   getLastError(), getErrorMessage() — are safe from any task.
// - Control methods — stop(), reset(), reconnect() — are safe from any task.
// - Scalar runtime state (_state, _healthy) uses volatile for lock-free
//   single-writer (taskLoop) / multiple-reader access.
// - NtripStats is protected by statsMutex (snapshot copy on read).

class NtripClient {
public:
  // ── Lifecycle ──────────────────────────────────────────────────────────
  // Required call order: begin() → startTask() → … → stopTask()

  bool begin(const NtripConfig& cfg, Print& gnss);
  bool begin(const NtripConfig& cfg, HardwareSerial& gnss);

#if NTRIP_CLIENT_ENABLE_TASK
  /// Start background FreeRTOS task. Returns false if already running.
  bool startTask(uint8_t core = 0);
  /// Signal the task to stop and wait for clean exit.
  bool stopTask();
  /// True if the background task is currently active.
  bool isTaskRunning() const;
#endif

  // ── State queries (thread-safe, non-blocking) ─────────────────────────

  bool isStreaming() const;
  bool isHealthy() const;
  NtripState state() const;
  NtripStats getStats() const;
  NtripError getLastError() const;
  String getErrorMessage() const;

  // ── Control (thread-safe) ─────────────────────────────────────────────

  void stop();
  void reset();
  void reconnect();

  // ── Configuration ─────────────────────────────────────────────────────

  void setLogger(NtripLogFn logger);
  static bool validateConfig(const NtripConfig& cfg, String& errorOut);

private:
  static void taskEntry(void* arg);
  void taskLoop();
  bool connectCaster(const NtripConfig& cfg);
  bool connectCasterWithVersion(const NtripConfig& cfg,
                                bool useRev2,
                                NtripError& err,
                                String& errMsg);
  void disconnect();
  void setError(NtripError err, const String& msg);
  void logf(NtripLogLevel level, const char* fmt, ...) const;

  WiFiClient client;
  Print* gnssOutput = nullptr;
  NtripConfig config;

  // Volatile scalars — written by taskLoop, readable from any task.
  volatile NtripState _state = NtripState::DISCONNECTED;
  volatile bool _healthy = false;
  volatile bool _running = false;
  volatile unsigned long lastHealth = 0;
  volatile unsigned long lastAttempt = 0;
  volatile uint8_t failures = 0;

  // Aggregate stats — protected by statsMutex.
  NtripStats _stats;
  SemaphoreHandle_t statsMutex = nullptr;

#if NTRIP_CLIENT_ENABLE_TASK
  TaskHandle_t _taskHandle = nullptr;
#endif

  NtripLogFn logFn = nullptr;
};
