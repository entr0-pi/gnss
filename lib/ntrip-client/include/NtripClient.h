#pragma once
#include <WiFiClient.h>
#include <Arduino.h>

// NtripClient: lightweight NTRIP (Networked Transport of RTCM via Internet Protocol)
// client intended for embedded targets. It connects to an NTRIP caster, streams
// RTCM correction data to a GNSS receiver, and monitors stream health.
//
// Tuning notes:
// - retryDelayMs / maxTries: trade faster recovery vs. network/caster load.
// - healthTimeoutMs / passiveSampleMs / requiredValidFrames: trade sensitivity
//   to stalled streams vs. tolerance to intermittent data.
// - bufferSize: trade RAM vs. ability to read larger bursts without overflow.
// - connectTimeoutMs: trade faster failover vs. tolerance to slow networks.

// Version information
#define NTRIP_CLIENT_VERSION "2.0.0"

// Configuration structure for connection, validation, and recovery behavior.
struct NtripConfig {
  String host;
  uint16_t port = 2101;
  String mount;
  String user;
  String pass;
  uint8_t maxTries = 5;
  
  // Advanced settings (optional)
  // retryDelayMs: delay between connection attempts when disconnected.
  // healthTimeoutMs: time without valid RTCM before declaring "zombie" stream.
  // passiveSampleMs: how often to scan for RTCM preamble once validated.
  // requiredValidFrames: number of valid frames required to accept a stream.
  // bufferSize: read buffer size for TCP data bursts.
  // connectTimeoutMs: TCP connect and HTTP response timeout.
  uint32_t retryDelayMs = 30000;      // Time between retry attempts
  uint32_t healthTimeoutMs = 60000;   // Zombie stream detection timeout
  uint32_t passiveSampleMs = 5000;    // Passive health check interval
  uint8_t requiredValidFrames = 3;    // Frames needed for validation
  uint16_t bufferSize = 1024;         // Read buffer size
  uint32_t connectTimeoutMs = 5000;   // TCP connection timeout
};

// Connection states
enum class NtripState {
  DISCONNECTED,
  CONNECTING,
  STREAMING,
  LOCKED_OUT
};

// Error codes
enum class NtripError {
  NONE = 0,
  TCP_CONNECT_FAILED,
  HTTP_AUTH_FAILED,
  HTTP_MOUNT_NOT_FOUND,
  HTTP_TIMEOUT,
  HTTP_UNKNOWN_ERROR,
  STREAM_VALIDATION_FAILED,
  ZOMBIE_STREAM_DETECTED,
  MAX_RETRIES_EXCEEDED
};

// Statistics structure
struct NtripStats {
  uint32_t totalFrames = 0;           // Valid RTCM frames received
  uint32_t crcErrors = 0;             // Failed CRC checks
  uint32_t bytesReceived = 0;         // Total bytes from caster
  uint32_t reconnects = 0;            // Number of reconnection attempts
  uint32_t totalUptime = 0;           // Milliseconds in STREAMING state
  uint16_t lastMessageType = 0;       // Last RTCM message ID received
  unsigned long lastFrameTime = 0;    // Timestamp of last valid frame
  unsigned long connectionStart = 0;  // When current connection started
  NtripError lastError = NtripError::NONE;
  String lastErrorMessage;            // Human-readable error
};

class NtripClient {
public:
  /**
   * Initialize the NTRIP client with configuration
   * @param cfg Configuration structure
   * @param gnss Serial port connected to GNSS receiver
   * @return true if initialization successful
   */
  bool begin(const NtripConfig& cfg, HardwareSerial& gnss);
  
  /**
   * Start the NTRIP task on specified core
   * @param core CPU core (0 or 1)
   */
  void startTask(uint8_t core = 0);
  
  /**
   * Check if client is actively streaming data
   * @return true if connected and streaming
   */
  bool isStreaming() const;
  
  /**
   * Check if stream is healthy (receiving valid RTCM)
   * @return true if healthy
   */
  bool isHealthy() const;
  
  /**
   * Get current connection state
   * @return Current NtripState
   */
  NtripState state() const;
  
  /**
   * Get statistics about NTRIP performance
   * @return Stats structure
   */
  NtripStats getStats() const;
  
  /**
   * Get last error information
   * @return Error code
   */
  NtripError getLastError() const;
  
  /**
   * Get human-readable error message
   * @return Error message string
   */
  String getErrorMessage() const;
  
  /**
   * Stop the client and enter lockout state
   */
  void stop();
  
  /**
   * Reset lockout and allow reconnection
   */
  void reset();
  
  /**
   * Force immediate reconnection attempt
   */
  void reconnect();

private:
  // FreeRTOS task entry point.
  static void taskEntry(void* arg);
  // Main loop handling connect, stream validation, and health checks.
  void taskLoop();
  // Establish TCP connection and validate HTTP response.
  bool connectCaster(const NtripConfig& cfg);
  // Close socket and move to DISCONNECTED state.
  void disconnect();
  // Store error code/message in stats (thread-safe).
  void setError(NtripError err, const String& msg);
  
  WiFiClient client;
  HardwareSerial* gnssSerial = nullptr;
  NtripConfig config;
  
  NtripState _state = NtripState::DISCONNECTED;
  bool _healthy = false;
  NtripStats _stats;
  
  unsigned long lastHealth = 0;
  unsigned long lastAttempt = 0;
  uint8_t failures = 0;
  
  mutable SemaphoreHandle_t statsMutex = nullptr;
  mutable SemaphoreHandle_t configMutex = nullptr;
};
