/*
  ESP32-C3 (Arduino) BLE <-> UART bridge for GNSS
  + WiFi STA + WebServer for Status

  - BLE (NUS) using NimBLE-Arduino (h2zero)
  - FreeRTOS StreamBuffers as ring buffers
  - iPhone compatible: subscribe callback tracks notify enable
  - Backpressure aware: does not drain UART->BLE faster than notify succeeds

  Direction (byte stream, no framing):
    GNSS -> iPhone : UART RX -> stream buffer -> BLE notify (NUS TX)
    iPhone -> GNSS : BLE write (NUS RX) -> stream buffer -> UART TX

  Pins (ESP32-C3):
    GPIO20 = RX  (connect to GNSS TX)
    GPIO21 = TX  (connect to GNSS RX)

  Notes:
    - Streams raw bytes (NMEA + any other GNSS output)
    - RTCM bursts are buffered; when buffers fill, new bytes are dropped (no counters)
*/

#include <Arduino.h>

// ---- BLE ----
// NimBLE-Arduino implements BLE peripheral/server, characteristics, notifications, callbacks, etc.
#if BLE_ENABLE
#include <NimBLEDevice.h>
#endif

// ---- (WiFi + WebServer) ----
// WiFi STA mode connects to an existing hotspot/router and hosts a small status web server.
#include "app.h"
#define MODULE_LOG 1
#include "logger.h"
#include "gnss_config.h"
#include "ntrip_client.h"
#include "wifi_config.h"

#if WIFI_ENABLE
#include <WiFi.h>
#endif

#if WEBUI_ENABLE
#include <WebServer.h>
#include "web_ui.h"   // UI routes + snapshot structs (your own module)
#endif

#if TCP_ENABLE
#include <WiFiServer.h>
#include <WiFiClient.h>
#endif

// ---- NMEA ----
// Optional NMEA parsing (compile-time). If disabled, bytes are still streamed.
#if NMEA_ENABLE
#include "nmea_gps.h"
#endif

// ---- FreeRTOS primitives used by ESP32 Arduino ----
// StreamBuffers are lock-free-ish byte FIFOs ideal for producer/consumer tasks.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "esp_task_wdt.h"

#if WEBUI_ENABLE
// ---- Webserver ----
// Simple synchronous HTTP server from the Arduino core. We call handleClient() in loop().
WebServer server(80);
#endif

#if BLE_ENABLE
// ---------------- BLE (NUS UUIDs) ----------------
// Nordic UART Service (NUS) UUIDs:
// - Service UUID
// - RX characteristic (phone -> device): WRITE / WRITE_NR
// - TX characteristic (device -> phone): NOTIFY
static NimBLEUUID NUS_SERVICE_UUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
static NimBLEUUID NUS_RX_UUID     ("6E400002-B5A3-F393-E0A9-E50E24DCCA9E"); // write from phone
static NimBLEUUID NUS_TX_UUID     ("6E400003-B5A3-F393-E0A9-E50E24DCCA9E"); // notify to phone

// ---------------- Globals ----------------
// Pointers to the NimBLE server and TX characteristic so tasks/callbacks can use them.
static NimBLEServer*         g_server    = nullptr;
static NimBLECharacteristic* g_txChar    = nullptr;

// Connection/subscription state gates the BLE TX task.
// g_connected: true when a central is connected.
// g_notifyEn:  true when the central enabled notifications on the TX characteristic.
static bool                  g_connected = false;
static bool                  g_notifyEn  = false;
#endif

#if BLE_ENABLE
// StreamBuffers (static allocation)
// Using xStreamBufferCreateStatic() avoids dynamic allocations and fragmentation.
// - g_sb_uart2ble: bytes from GNSS UART RX -> BLE notify task
// - g_sb_ble2uart: bytes from BLE writes -> GNSS UART TX task
static StaticStreamBuffer_t  g_sb_uart2ble_struct;
static StaticStreamBuffer_t  g_sb_ble2uart_struct;
static uint8_t               g_sb_uart2ble_storage[SB_UART_TO_BLE_SIZE];
static uint8_t               g_sb_ble2uart_storage[SB_BLE_TO_UART_SIZE];
static StreamBufferHandle_t  g_sb_uart2ble = nullptr;
static StreamBufferHandle_t  g_sb_ble2uart = nullptr;
#endif

#if TCP_ENABLE
// StreamBuffers for TCP (static allocation).
// - g_sb_uart2tcp: bytes from GNSS UART RX -> TCP client
// - g_sb_tcp2uart: bytes from TCP client -> GNSS UART TX
static StaticStreamBuffer_t  g_sb_uart2tcp_struct;
static StaticStreamBuffer_t  g_sb_tcp2uart_struct;
static uint8_t               g_sb_uart2tcp_storage[SB_UART_TO_TCP_SIZE];
static uint8_t               g_sb_tcp2uart_storage[SB_TCP_TO_UART_SIZE];
static StreamBufferHandle_t  g_sb_uart2tcp = nullptr;
static StreamBufferHandle_t  g_sb_tcp2uart = nullptr;

// TCP server (single-client).
static WiFiServer g_tcpServer(TCP_PORT);
static WiFiClient g_tcpClient;
#endif

#if NTRIP_CLIENT_ENABLE
// StreamBuffer for NTRIP (static allocation).
// - g_sb_ntrip2uart: bytes from NTRIP client -> GNSS UART TX
static StaticStreamBuffer_t  g_sb_ntrip2uart_struct;
static uint8_t               g_sb_ntrip2uart_storage[SB_NTRIP_TO_UART_SIZE];
static StreamBufferHandle_t  g_sb_ntrip2uart = nullptr;
#endif
#if BLE_ENABLE
// ========================= BLE STATUS (BLOCK) =========================
// Small status accumulator used by the web UI snapshots.
//
// Notes about volatile:
// - These fields can be touched by multiple tasks/callback contexts.
// - volatile avoids compiler reordering/caching; it is NOT a full concurrency primitive,
//   but for simple counters/flags it is usually acceptable here.
struct BleStatus {
  volatile bool     connected      = false;  // last known connected state
  volatile uint16_t mtu            = 0;      // last negotiated MTU (from connInfo)
  volatile uint64_t txBytes        = 0;      // bytes successfully notified (device -> phone)
  volatile uint64_t rxBytes        = 0;      // bytes received from phone writes (phone -> device)
  volatile uint32_t uart2bleDrops  = 0;      // drops when UART->BLE buffer is full
  volatile uint32_t ble2uartDrops  = 0;      // drops when BLE->UART buffer is full

  // Convenience: reset the counters (does not affect connection state).
  void resetCounters() {
    txBytes = 0; rxBytes = 0;
    uart2bleDrops = 0; ble2uartDrops = 0;
  }
};

static BleStatus g_bleStatus;
static uint16_t g_ble_mtu = BLE_MTU;
// ======================= END BLE STATUS (BLOCK) =======================
#endif

#if TCP_ENABLE
// ========================= TCP STATUS (BLOCK) =========================
// Simple TCP status accumulator used by the web UI snapshots.
struct TcpStatus {
  volatile bool     connected      = false;  // last known connected state
  volatile uint64_t txBytes        = 0;      // bytes sent to client
  volatile uint64_t rxBytes        = 0;      // bytes received from client
  volatile uint32_t uart2tcpDrops  = 0;      // drops when UART->TCP buffer is full
  volatile uint32_t tcp2uartDrops  = 0;      // drops when TCP->UART buffer is full

  void resetCounters() {
    txBytes = 0; rxBytes = 0;
    uart2tcpDrops = 0; tcp2uartDrops = 0;
  }
};

static TcpStatus g_tcpStatus;
// ======================= END TCP STATUS (BLOCK) =======================
#endif
#if WEBUI_ENABLE
// ---- Snapshot getter for web_ui.cpp (declared in web_ui.h) ----
// web_ui.cpp calls this to build JSON for the status page without directly
// depending on NimBLE internals.
//
// Important: This function reads the current global state and copies it into `out`.
bool webui_get_ble_snapshot(WebuiBleSnapshot& out) {
#if BLE_ENABLE
  out.connected     = g_bleStatus.connected;
  out.mtu           = g_bleStatus.mtu;

  // Keep JSON compact: truncate counters to 32-bit here.
  // If you want exact long-running counters, change WebuiBleSnapshot to uint64_t.
  out.txBytes = (uint32_t)g_bleStatus.txBytes;
  out.rxBytes = (uint32_t)g_bleStatus.rxBytes;
  out.uart2bleDrops = g_bleStatus.uart2bleDrops;
  out.ble2uartDrops = g_bleStatus.ble2uartDrops;

  return true;
#else
  (void)out;
  return false;
#endif
}
#endif

#if WEBUI_ENABLE && TCP_ENABLE
bool webui_get_tcp_snapshot(WebuiTcpSnapshot& out) {
  out.connected     = g_tcpStatus.connected;
  out.txBytes       = (uint32_t)g_tcpStatus.txBytes;
  out.rxBytes       = (uint32_t)g_tcpStatus.rxBytes;
  out.uart2tcpDrops = g_tcpStatus.uart2tcpDrops;
  out.tcp2uartDrops = g_tcpStatus.tcp2uartDrops;
  return true;
}
#endif

#if WEBUI_ENABLE && NMEA_ENABLE
// GPS snapshot getter for the web UI.
// It copies data from your NMEA module (nmea_gps.*) to the web_ui snapshot struct.
bool webui_get_gps_snapshot(WebuiGpsSnapshot& out) {
  NmeaGpsSnapshot s{};
  if (!nmea_get_snapshot(s)) return false;

  out.valid      = s.valid;
  out.lat        = s.lat;
  out.lon        = s.lon;
  out.speedKmh   = s.speedKmh;

  out.satsUsed   = s.satsUsed;
  out.fixQuality = s.fixQuality;
  out.fixType    = s.fixType;
  out.hdop       = s.hdop;

  out.hAcc_m     = s.hAcc_m;
  out.vAcc_m     = s.vAcc_m;
  out.accSource  = s.accSource;

  out.timeValid  = s.timeValid;
  out.hour       = s.hour;
  out.minute     = s.minute;
  out.second     = s.second;

  out.year       = s.year;
  out.month      = s.month;
  out.day        = s.day;

  out.ageMs      = s.ageMs;

  // Copy satellite details (GSV).
  out.satCount = s.satCount;
  for (uint8_t i = 0; i < s.satCount; i++) {
    out.sats[i].nr            = s.sats[i].nr;
    out.sats[i].elevation     = s.sats[i].elevation;
    out.sats[i].azimuth       = s.sats[i].azimuth;
    out.sats[i].snr           = s.sats[i].snr;
    out.sats[i].constellation = s.sats[i].constellation;
  }

  return true;
}
#endif

#if WEBUI_ENABLE && NTRIP_CLIENT_ENABLE
bool webui_get_ntrip_snapshot(WebuiNtripSnapshot& out) {
  NtripClientSnapshot snap{};
  if (!ntrip_client_get_snapshot(snap)) return false;
  out.connected = snap.connected;
  out.healthy = snap.healthy;
  out.streaming = snap.streaming;
  out.bytesReceived = snap.bytesReceived;
  out.totalFrames = snap.totalFrames;
  out.lastMessageType = snap.lastMessageType;
  out.lastFrameAgeMs = snap.lastFrameAgeMs;
  return true;
}
#endif

#if BLE_ENABLE
// ---------------- BLE Callbacks ----------------
// NimBLE calls these on BLE events (connect/disconnect/subscribe/write).
//
// These callbacks should do minimal work: set flags, update counters, reset buffers.
// Avoid heavy operations or long delays in callbacks.

class ServerCallbacks : public NimBLEServerCallbacks {
  // Called when a central connects to our peripheral.
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    (void)pServer; (void)connInfo;

    // Mark link as connected.
    g_connected = true;

    // Notifications are NOT automatically enabled; the phone must subscribe.
    // We reset it here and rely on onSubscribe() to set it properly.
    g_notifyEn  = false;

    // ---- status hook ----
    // Capture connection state and negotiated MTU for the web status page.
    g_bleStatus.connected = true;
    uint16_t negotiated = connInfo.getMTU();
    if (negotiated >= 23) {
      g_ble_mtu = negotiated;
      g_bleStatus.mtu = negotiated;
    } else {
      g_ble_mtu = BLE_MTU;
      g_bleStatus.mtu = BLE_MTU;
    }
  }

  // Called when the central disconnects.
  // reason is an integer reason code from the stack.
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    (void)pServer; (void)connInfo; (void)reason;

    // Update connection flags.
    g_connected = false;
    g_notifyEn  = false;

    // Flush pending NMEA so next connect is "live":
    // If the phone reconnects, we don't want to dump a backlog of stale data.
    if (g_sb_uart2ble) xStreamBufferReset(g_sb_uart2ble);

    // ---- status hook ----
    g_bleStatus.connected = false;
    g_bleStatus.mtu = 0;


    // Resume advertising so another central can connect.
    NimBLEDevice::startAdvertising();
  }
};

class TxCallbacks : public NimBLECharacteristicCallbacks {
  // Called when a central subscribes/unsubscribes to the TX notify characteristic.
  // subValue is a bitmask (bit0 indicates notifications enabled).
  void onSubscribe(NimBLECharacteristic* pCharacteristic,
                   NimBLEConnInfo& connInfo,
                   uint16_t subValue) override {
    (void)pCharacteristic; (void)connInfo;

    // bit0 = notifications enabled
    g_notifyEn = (subValue & 0x0001);

    // When notifications become enabled, drop any backlog so stream starts live.
    // This ensures the phone sees current NMEA, not buffered "old" bytes.
    if (g_notifyEn && g_sb_uart2ble) xStreamBufferReset(g_sb_uart2ble);
  }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
  // Called when the central writes to the RX characteristic (phone -> device).
  // We treat this as raw bytes (typically RTCM corrections).
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    (void)connInfo;

    // Copy data from NimBLE into a std::string owned by the characteristic.
    // getValue() returns the full write payload.
    const std::string& v = pCharacteristic->getValue();
    if (v.empty() || !g_sb_ble2uart) return;

    // ---- status hook ----
    // Count how many bytes have come from the phone.
    g_bleStatus.rxBytes += v.size();

    // Best-effort push RTCM bytes into BLE->UART buffer.
    // Timeout = 0 means "do not block"; bytes may be dropped if buffer is full.
    size_t sent = xStreamBufferSend(g_sb_ble2uart, v.data(), v.size(), 0);
    if (sent < v.size()) {
      g_bleStatus.ble2uartDrops += (uint32_t)(v.size() - sent);
    }
  }
};
#endif

// ----------- FUNCTIONS DECLARATIONS -----------
// Declared here so setup() can call them before their definitions below.

// Setup helper functions (extracted for cleaner setup())
static void initSerialAndConfig();
static void createStreamBuffers();
static void setupUartIfConfigured();
#if BLE_ENABLE
static void startBleServer();
#endif
#if WEBUI_ENABLE
static void initWebUiRoutes();
#endif
#if WIFI_ENABLE
static void connectWiFi();
#endif
#if WEBUI_ENABLE
static void startWebServer();
#endif
#if NMEA_ENABLE
static void initNmea();
#endif
static void startWorkerTasks();

// Loop helper functions (extracted for cleaner loop())
static void logLoopEntryOnce();
#if WIFI_ENABLE
static void maybeReconnectWiFi();
#endif
#if WEBUI_ENABLE
static void handleWebUi();
#endif
static void yieldToTasks();

// Core setup functions
#if BLE_ENABLE
static void setupBLE();
#endif
static void setupUART();
#if WIFI_ENABLE
static void setupWiFi();
#endif

// FreeRTOS task functions
static void task_uart_rx(void* arg);
#if BLE_ENABLE
static void task_ble_tx(void* arg);
#endif
static void task_uart_tx(void* arg);
#if TCP_ENABLE
static void task_tcp_io(void* arg);
#endif

// -------------------------------------------
// -------------- SETUP & LOOP ---------------
// -------------------------------------------

void setup() {
  // Initialize debug serial and load persisted GNSS configuration from NVS.
  initSerialAndConfig();

  // Allocate FreeRTOS StreamBuffers for inter-task byte streaming (UART<->BLE, UART<->TCP).
  createStreamBuffers();

  // Configure UART for GNSS communication if pins/baud are set; skips if not configured.
  setupUartIfConfigured();

  #if BLE_ENABLE
  // Initialize NimBLE stack, create NUS service, and start advertising.
  startBleServer();

  #endif

  #if WEBUI_ENABLE
  // Register HTTP routes for the status web UI (before server starts).
  initWebUiRoutes();
  #endif

  #if WIFI_ENABLE
  // Connect to WiFi in STA mode using stored or default credentials.
  connectWiFi();
  #endif

  #if WEBUI_ENABLE
  // Start the HTTP server to serve the web UI.
  startWebServer();
  #endif

  #if NMEA_ENABLE
  // Initialize the optional NMEA sentence parser.
  initNmea();
  #endif

  #if NTRIP_CLIENT_ENABLE
  // Start the NTRIP client and configuration monitor.
  ntrip_client_setup(g_sb_ntrip2uart);
  #endif

  // Create FreeRTOS tasks for UART RX/TX, BLE TX, and optionally TCP I/O.
  startWorkerTasks();
}

void loop() {
  // Log a one-time banner on first iteration to indicate loop has started.
  logLoopEntryOnce();

  #if WIFI_ENABLE
  // Attempt WiFi reconnection if disconnected (throttled to every 5 seconds).
  maybeReconnectWiFi();
  #endif

  #if WEBUI_ENABLE
  // Poll the HTTP server to process incoming web requests.
  handleWebUi();
  #endif

  #if NTRIP_CLIENT_ENABLE
  // Periodic NTRIP status logging and lockout handling.
  ntrip_client_loop();
  #endif

  // Yield CPU time to other FreeRTOS tasks.
  yieldToTasks();
}

// -------------------------------------------
// ----------- SETUP HELPER FUNCTIONS --------
// -------------------------------------------

/**
 * initSerialAndConfig()
 * Initializes the debug serial port (USB CDC) and loads the persisted GNSS
 * configuration from NVS. A short delay allows the USB CDC and RTOS
 * scheduler to stabilize after boot.
 */
static void initSerialAndConfig() {
  Serial.begin(SERIAL_BAUD);
  vTaskDelay(pdMS_TO_TICKS(200));
  LOG_I("SETUP", "Loading config...");
  gnss_config_begin();
}

/**
 * createStreamBuffers()
 * Allocates FreeRTOS StreamBuffers using static memory for inter-task
 * communication. These lock-free byte FIFOs connect:
 *   - UART RX -> BLE TX (g_sb_uart2ble)
 *   - BLE RX -> UART TX (g_sb_ble2uart)
 *   - UART RX -> TCP TX (g_sb_uart2tcp) [if TCP_ENABLE]
 *   - TCP RX -> UART TX (g_sb_tcp2uart) [if TCP_ENABLE]
 * Halts with an infinite loop if allocation fails.
 */
static void createStreamBuffers() {
  LOG_I("SETUP", "Creating stream buffers...");

#if BLE_ENABLE
  g_sb_uart2ble = xStreamBufferCreateStatic(
      SB_UART_TO_BLE_SIZE, SB_TRIGGER_LEVEL,
      g_sb_uart2ble_storage, &g_sb_uart2ble_struct);

  g_sb_ble2uart = xStreamBufferCreateStatic(
      SB_BLE_TO_UART_SIZE, SB_TRIGGER_LEVEL,
      g_sb_ble2uart_storage, &g_sb_ble2uart_struct);
#endif

#if TCP_ENABLE
  g_sb_uart2tcp = xStreamBufferCreateStatic(
      SB_UART_TO_TCP_SIZE, SB_TRIGGER_LEVEL,
      g_sb_uart2tcp_storage, &g_sb_uart2tcp_struct);

  g_sb_tcp2uart = xStreamBufferCreateStatic(
      SB_TCP_TO_UART_SIZE, SB_TRIGGER_LEVEL,
      g_sb_tcp2uart_storage, &g_sb_tcp2uart_struct);
#endif

#if NTRIP_CLIENT_ENABLE
  g_sb_ntrip2uart = xStreamBufferCreateStatic(
      SB_NTRIP_TO_UART_SIZE, SB_TRIGGER_LEVEL,
      g_sb_ntrip2uart_storage, &g_sb_ntrip2uart_struct);
#endif

  bool ok = true;
#if BLE_ENABLE
  ok = ok && g_sb_uart2ble && g_sb_ble2uart;
#endif
#if TCP_ENABLE
  ok = ok && g_sb_uart2tcp && g_sb_tcp2uart;
#endif
#if NTRIP_CLIENT_ENABLE
  ok = ok && g_sb_ntrip2uart;
#endif

  if (!ok) {
    LOG_E("SETUP", "Stream buffer creation failed!");
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

/**
 * setupUartIfConfigured()
 * Checks the persisted GNSS configuration for valid UART pins and baud rate.
 * If configured, initializes Serial1 for GNSS communication.
 * CRITICAL: Must be called BEFORE WiFi/BLE init to avoid ESP32-C3 hang issues.
 */
static void setupUartIfConfigured() {
  const GnssConfig& cfg = gnss_config_get();
  if (cfg.rx_pin == -1 || cfg.tx_pin == -1 || cfg.baud == 0) {
    LOG_W("SETUP", "UART not configured - configure via web UI");
  } else {
    LOG_I("SETUP", "Setting up UART...");
    setupUART();
  }
}

/**
 * startBleServer()
 * Initializes the NimBLE stack, creates the Nordic UART Service (NUS),
 * and starts BLE advertising. After this call, BLE centrals can discover
 * and connect to the device.
 */
#if BLE_ENABLE
static void startBleServer() {
  LOG_I("SETUP", "Starting BLE...");
  setupBLE();
}
#endif

#if WEBUI_ENABLE
/**
 * initWebUiRoutes()
 * Registers HTTP routes and static assets for the status web UI.
 * Must be called before startWebServer(). Routes are registered but
 * the server does not accept connections until server.begin() is called.
 */
static void initWebUiRoutes() {
  LOG_I("SETUP", "Initializing WebUI...");
  webui_begin(server, STA_DNS);
}
#endif

#if WIFI_ENABLE
/**
 * connectWiFi()
 * Connects to WiFi in STA mode using credentials from NVS or
 * compile-time defaults. Blocks for up to 10 seconds waiting for
 * connection; if it fails, loop() will retry periodically.
 */
static void connectWiFi() {
  LOG_I("SETUP", "Connecting to WiFi...");
  setupWiFi();
  LOG_I("SETUP", "WiFi setup complete");
}
#endif

#if WEBUI_ENABLE
/**
 * startWebServer()
 * Starts the HTTP server listening on port 80. After this call,
 * the web UI becomes accessible at the device's IP address.
 */
static void startWebServer() {
  LOG_I("SETUP", "Starting web server...");
  server.begin();
  LOG_I("SETUP", "Web server started");
}
#endif

#if NMEA_ENABLE
/**
 * initNmea()
 * Initializes the optional NMEA sentence parser module. When enabled,
 * incoming GNSS bytes are parsed to extract position, time, and
 * satellite information for the web UI.
 */
static void initNmea() {
  LOG_I("SETUP", "Initializing NMEA...");
  nmea_begin();
}
#endif

/**
 * startWorkerTasks()
 * Creates FreeRTOS tasks for the main data processing loops:
 *   - task_uart_rx: Reads GNSS bytes from Serial1, feeds NMEA parser, buffers for BLE/TCP
 *   - task_uart_tx: Writes correction data (from BLE/TCP) to GNSS via Serial1
 *   - task_ble_tx:  Sends buffered GNSS data to connected BLE central via notifications
 *   - task_tcp_io:  Handles TCP client connections and bidirectional data (if TCP_ENABLE)
 * UART tasks run at priority 3, BLE/TCP tasks at priority 2.
 */
static void startWorkerTasks() {
  LOG_I("SETUP", "Creating tasks...");
  xTaskCreate(task_uart_rx, "uart_rx", 4096, nullptr, 3, nullptr);
  xTaskCreate(task_uart_tx, "uart_tx", 4096, nullptr, 3, nullptr);
#if BLE_ENABLE
  xTaskCreate(task_ble_tx,  "ble_tx",  4096, nullptr, 2, nullptr);
#endif
#if TCP_ENABLE
  g_tcpServer.begin();
  g_tcpServer.setNoDelay(true);
  xTaskCreate(task_tcp_io,  "tcp_io",  4096, nullptr, 2, nullptr);
#endif
  LOG_I("SETUP", "Setup complete!");
}

// -------------------------------------------
// ----------- LOOP HELPER FUNCTIONS ---------
// -------------------------------------------

/**
 * logLoopEntryOnce()
 * Prints a one-time banner to serial when the main loop first executes.
 * Useful for confirming that setup() completed and loop() is running.
 */
static void logLoopEntryOnce() {
  static bool first_loop = true;
  if (first_loop) {
    LOG_I("LOOP", "Entered main loop");
    first_loop = false;
  }
}

#if WIFI_ENABLE
/**
 * maybeReconnectWiFi()
 * Checks WiFi connection status and attempts reconnection if disconnected.
 * Throttled to one attempt every 5 seconds to avoid spamming the WiFi stack.
 */
static void maybeReconnectWiFi() {
  static unsigned long last_wifi_attempt = 0;
  if (WiFi.status() != WL_CONNECTED) {
    const unsigned long now = millis();
    if ((now - last_wifi_attempt) > 5000) {
      WiFi.reconnect();
      last_wifi_attempt = now;
    }
  }
}
#endif

#if WEBUI_ENABLE
/**
 * handleWebUi()
 * Polls the HTTP server to process one pending client request per call.
 * Must be called frequently in loop() for responsive web UI.
 */
static void handleWebUi() {
  server.handleClient();
}
#endif

/**
 * yieldToTasks()
 * Yields CPU time to other FreeRTOS tasks with a small delay.
 * Prevents loop() from monopolizing the CPU on ESP32 Arduino.
 */
static void yieldToTasks() {
  delay(2);
}

// -------------------------------------------
// ----------- CORE SETUP FUNCTIONS ----------
// -------------------------------------------

#if WIFI_ENABLE
static void setupWiFi() {
  auto parseIpField = [](const String& value, IPAddress& out) -> bool {
    return !value.isEmpty() && out.fromString(value);
  };

  // Station mode: connect to an existing access point / hotspot.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.setSleep(true);

  WifiConfig file_cfg{};
  bool use_file_cfg = false;
  String wifi_error;
  if (wifi_config_load(file_cfg, &wifi_error)) {
    use_file_cfg = true;
    LOG_I("WiFi", "Loaded config from NVS");
  } else {
    LOG_W("WiFi", "NVS config missing/invalid: %s", wifi_error.c_str());
  }

  const char* ssid = use_file_cfg ? file_cfg.ssid.c_str() : STA_SSID;
  const char* pass = use_file_cfg ? file_cfg.pass.c_str() : STA_PASS;
  const bool use_dhcp = use_file_cfg && file_cfg.dhcp;
  const IPAddress ip = use_file_cfg ? file_cfg.ip : STA_IP;
  const IPAddress gw = use_file_cfg ? file_cfg.gw : STA_GW;
  const IPAddress subnet = use_file_cfg ? file_cfg.subnet : STA_SUBNET;
  const IPAddress dns = use_file_cfg ? file_cfg.dns : STA_DNS;

  LOG_I("WiFi", "Config source: %s", use_file_cfg ? "NVS" : "compile-time");
  LOG_I("WiFi", "SSID: '%s' (len=%u)", ssid ? ssid : "", ssid ? (unsigned)strlen(ssid) : 0U);
  LOG_I("WiFi", "PASS length: %u", pass ? (unsigned)strlen(pass) : 0U);
  LOG_I("WiFi", "DHCP: %s", use_dhcp ? "true" : "false");
  if (!use_dhcp) {
    LOG_I("WiFi", "Static IP: %s", ip.toString().c_str());
    LOG_I("WiFi", "Gateway : %s", gw.toString().c_str());
    LOG_I("WiFi", "Subnet  : %s", subnet.toString().c_str());
    LOG_I("WiFi", "DNS     : %s", dns.toString().c_str());
  }

  // Apply static IP configuration for the STA interface.
  if (use_dhcp) {
    if (!WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0))) {
      LOG_E("WiFi", "DHCP config failed!");
    } else {
      LOG_I("WiFi", "DHCP config applied");
    }
  } else {
    if (!WiFi.config(ip, gw, subnet, dns)) {
      LOG_E("WiFi", "Config failed!");
    } else {
      LOG_I("WiFi", "Static config applied");
    }
  }

  // Start connection attempt using SSID/PASS.
  LOG_I("WiFi", "Calling WiFi.begin(...)");
  WiFi.begin(ssid, pass);

  // Wait up to 10 seconds for connection with yield to prevent watchdog.
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 10000) {
    delay(500);
    LOG_I("WiFi", ".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    LOG_I("WiFi", "Connected: %s", WiFi.localIP().toString().c_str());
    LOG_I("WiFi", "Gateway: %s", WiFi.gatewayIP().toString().c_str());
    LOG_I("WiFi", "Subnet : %s", WiFi.subnetMask().toString().c_str());
    LOG_I("WiFi", "DNS[0] : %s", WiFi.dnsIP(0).toString().c_str());
    LOG_I("WiFi", "DNS[1] : %s", WiFi.dnsIP(1).toString().c_str());
  } else {
    LOG_W("WiFi", "Connection failed, will retry in loop");
  }
}
#endif

// ---------------- Setup BLE ----------------
#if BLE_ENABLE
static void setupBLE() {
  // Initialize NimBLE and set the device name used in advertising.
  NimBLEDevice::init(BLE_DEVICE_NAME);

  // RF power level; higher can improve range but increases current consumption.
  NimBLEDevice::setPower(ESP_PWR_LVL_P6);

  // Request a larger MTU. The negotiated MTU depends on the phone's capabilities too.
  NimBLEDevice::setMTU(BLE_MTU);

  // Create BLE GATT server and attach server-level callbacks.
  g_server = NimBLEDevice::createServer();
  g_server->setCallbacks(new ServerCallbacks());

  // Create the Nordic UART Service (NUS).
  NimBLEService* svc = g_server->createService(NUS_SERVICE_UUID);

  // TX characteristic (device -> phone): NOTIFY
  // The phone subscribes to this to receive the stream.
  g_txChar = svc->createCharacteristic(NUS_TX_UUID, NIMBLE_PROPERTY::NOTIFY);
  g_txChar->setCallbacks(new TxCallbacks());

  // RX characteristic (phone -> device): WRITE and WRITE_NR (write without response)
  // This receives RTCM / commands / any bytes written by the phone.
  NimBLECharacteristic* rxChar =
      svc->createCharacteristic(NUS_RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rxChar->setCallbacks(new RxCallbacks());

  // Start the service so it becomes visible in the GATT database.
  svc->start();

  // Configure advertising: include the service UUID so centrals can discover NUS.
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SERVICE_UUID);

  // Scan response allows extra data (like name/service) in response packets.
  adv->enableScanResponse(true);

  // Start advertising now.
  adv->start();
}
#endif

// ---------------- Setup UART ----------------
static void setupUART() {
  const GnssConfig& cfg = gnss_config_get();
  LOG_I("UART", "Configuring: RX=%d, TX=%d, Baud=%u", cfg.rx_pin, cfg.tx_pin, cfg.baud);

  Serial1.begin(cfg.baud, SERIAL_8N1, cfg.rx_pin, cfg.tx_pin);
  delay(100); // Give UART time to initialize

  LOG_I("UART", "UART configured successfully");
}

bool gnss_apply_runtime_config(const GnssConfig& cfg, String* error) {
  if (!gnss_config_validate(cfg, error)) return false;

  Serial1.flush();
  Serial1.end();
  Serial1.begin(cfg.baud, SERIAL_8N1, cfg.rx_pin, cfg.tx_pin);

  if (!gnss_config_save(cfg)) {
    if (error) *error = "Failed to persist config to NVS.";
    return false;
  }

  return true;
}

// UART RX task:
// Continuously reads bytes from the GNSS (Serial1) and pushes them into the
// UART->BLE stream buffer. Also feeds the NMEA parser if enabled.
static void task_uart_rx(void* arg) {
  (void)arg;

  // Scratch buffer for reads. Size is UART_CHUNK, so we batch reads efficiently.
  uint8_t tmp[UART_CHUNK];

  for (;;) {
    // Query how many bytes are currently available in the UART RX queue.
    int avail = Serial1.available();
    if (avail <= 0) {
      // No data: yield quickly to other tasks.
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }

    // Read up to tmp capacity (or up to what's available).
    // readBytes blocks until timeout OR requested bytes read; but since we limit to avail,
    // it typically returns quickly.
    int n = Serial1.readBytes(tmp, (size_t)min(avail, (int)sizeof(tmp)));

    if (n > 0) {
      // Optional: parse NMEA from the same byte stream (doesn't affect forwarding).
      #if NMEA_ENABLE
      nmea_feed_bytes(tmp, (size_t)n, millis());
      #endif

#if BLE_ENABLE
      // Push bytes into UART->BLE buffer only when BLE is actively consuming.
      // This avoids counting "drops" when BLE is idle but TCP is receiving the stream.
      if (g_connected && g_notifyEn && g_sb_uart2ble) {
        size_t sent = xStreamBufferSend(g_sb_uart2ble, tmp, (size_t)n, 0);
        if (sent < (size_t)n) {
          g_bleStatus.uart2bleDrops += (uint32_t)((size_t)n - sent);
        }
      }
#endif

      #if TCP_ENABLE
      // Mirror the same stream to TCP.
      if (g_sb_uart2tcp) {
        size_t sent = xStreamBufferSend(g_sb_uart2tcp, tmp, (size_t)n, 0);
        if (sent < (size_t)n) {
          g_tcpStatus.uart2tcpDrops += (uint32_t)((size_t)n - sent);
        }
      }
      #endif
    }
  }
}

#if BLE_ENABLE
// BLE TX task:
// Pulls bytes from UART->BLE buffer and sends them as BLE notifications when:
// - a central is connected AND
// - the central subscribed to notifications (CCCD enabled) AND
// - TX characteristic exists.
//
// Backpressure principle:
// We only pull bytes when we intend to send them; if notify fails we slow down.
static void task_ble_tx(void* arg) {
  (void)arg;

  // Scratch buffer for BLE notify payload.
  uint8_t out[BLE_NOTIFY_CHUNK];

  size_t max_payload = BLE_NOTIFY_CHUNK;
  if (g_ble_mtu > 3 && (g_ble_mtu - 3) < max_payload) {
    max_payload = g_ble_mtu - 3;
  }
  const size_t low_rate_threshold = max_payload / 2;

  for (;;) {
    // If not connected or not subscribed, don't drain the UART buffer endlessly.
    // This prevents building "notify backlog handling" complexity and keeps stream live.
    if (!(g_connected && g_notifyEn && g_txChar)) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    // Pull bytes from the UART->BLE stream buffer.
    // If there are no bytes, this will block up to BLE_TX_WAIT_TICKS.
    size_t got = 0;
    if (g_sb_uart2ble) {
      got = xStreamBufferReceive(g_sb_uart2ble, out, max_payload, BLE_TX_WAIT_TICKS);
    }

    // If nothing received, yield briefly and retry.
    if (got == 0) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    // Set characteristic value to the chunk and attempt notify.
    // notify() returns whether it was accepted by the stack.
    g_txChar->setValue(out, got);
    bool ok = g_txChar->notify();

    // ---- status hook ----
    // Only count bytes as "tx" when notify succeeded.
    if (ok) { g_bleStatus.txBytes += got; }

    // Pacing:
    // - On success, short yield.
    // - On failure, back off more to reduce pressure on the stack.
    if (ok) {
      if (got < low_rate_threshold) {
        vTaskDelay(pdMS_TO_TICKS(BLE_LOW_RATE_DELAY_MS));
      } else {
        vTaskDelay(BLE_OK_DELAY);
      }
    } else {
      vTaskDelay(BLE_FAIL_DELAY);
    }
  }
}
#endif

// UART TX task:
// Pulls bytes from BLE->UART buffer (typically RTCM corrections) and writes to GNSS (Serial1).
static void task_uart_tx(void* arg) {
  (void)arg;

  // Scratch buffer for UART writes.
  uint8_t tmp[UART_CHUNK];

  for (;;) {
    bool did_work = false;

    #if BLE_ENABLE
    // Non-blocking read from BLE->UART buffer.
    size_t got = 0;
    if (g_sb_ble2uart) {
      got = xStreamBufferReceive(g_sb_ble2uart, tmp, sizeof(tmp), 0);
    }

    // If we received bytes, forward them to GNSS.
    if (got > 0) {
      Serial1.write(tmp, got);
      did_work = true;
    }
    #endif

    #if TCP_ENABLE
    // Non-blocking read from TCP->UART buffer.
    size_t got_tcp = 0;
    if (g_sb_tcp2uart) {
      got_tcp = xStreamBufferReceive(g_sb_tcp2uart, tmp, sizeof(tmp), 0);
    }
    if (got_tcp > 0) {
      Serial1.write(tmp, got_tcp);
      did_work = true;
    }
    #endif

    #if NTRIP_CLIENT_ENABLE
    // Non-blocking read from NTRIP->UART buffer.
    size_t got_ntrip = 0;
    if (g_sb_ntrip2uart) {
      got_ntrip = xStreamBufferReceive(g_sb_ntrip2uart, tmp, sizeof(tmp), 0);
    }
    if (got_ntrip > 0) {
      Serial1.write(tmp, got_ntrip);
      did_work = true;
    }
    #endif

    if (!did_work) {
      vTaskDelay(pdMS_TO_TICKS(10));
    } else {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}

#if TCP_ENABLE
// TCP I/O task:
// - Accepts a single TCP client at a time.
// - Receives bytes from TCP -> stream buffer -> UART TX.
// - Sends bytes from UART->TCP buffer -> TCP client.
static void task_tcp_io(void* arg) {
  (void)arg;

  uint8_t out[UART_CHUNK];
  uint8_t in[UART_CHUNK];

  for (;;) {
    if (!g_tcpClient || !g_tcpClient.connected()) {
      WiFiClient newClient = g_tcpServer.available();
      if (newClient) {
        if (g_tcpClient) g_tcpClient.stop();
        g_tcpClient = newClient;
        g_tcpClient.setNoDelay(true);
        g_tcpStatus.connected = true;
        if (g_sb_uart2tcp) xStreamBufferReset(g_sb_uart2tcp);
      } else {
        g_tcpStatus.connected = false;
        vTaskDelay(pdMS_TO_TICKS(50));
        continue;
      }
    }

    // TCP -> UART
    int avail = g_tcpClient.available();
    if (avail > 0) {
      int n = g_tcpClient.read(in, (size_t)min(avail, (int)sizeof(in)));
      if (n > 0 && g_sb_tcp2uart) {
        g_tcpStatus.rxBytes += (uint32_t)n;
        size_t sent = xStreamBufferSend(g_sb_tcp2uart, in, (size_t)n, 0);
        if (sent < (size_t)n) {
          g_tcpStatus.tcp2uartDrops += (uint32_t)((size_t)n - sent);
        }
      }
    }

    // UART -> TCP
    size_t got = 0;
    if (g_sb_uart2tcp) {
      got = xStreamBufferReceive(g_sb_uart2tcp, out, sizeof(out), pdMS_TO_TICKS(20));
    }
    if (got > 0) {
      size_t wrote = g_tcpClient.write(out, got);
      if (wrote > 0) g_tcpStatus.txBytes += wrote;
      if (wrote < got) {
        g_tcpStatus.uart2tcpDrops += (uint32_t)(got - wrote);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
#endif
