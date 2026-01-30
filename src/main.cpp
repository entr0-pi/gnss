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
#include <NimBLEDevice.h>

// ---- (WiFi + WebServer) ----
// WiFi STA mode connects to an existing hotspot/router and hosts a small status web server.
#include "app.h"

#if WEBUI_ENABLE
#include <WiFi.h>
#include <WebServer.h>
#include "web_ui.h"   // UI routes + snapshot structs (your own module)
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

#if WEBUI_ENABLE
// ---- Webserver ----
// Simple synchronous HTTP server from the Arduino core. We call handleClient() in loop().
WebServer server(80);
#endif

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

#if WEBUI_ENABLE
// ---- Snapshot getter for web_ui.cpp (declared in web_ui.h) ----
// web_ui.cpp calls this to build JSON for the status page without directly
// depending on NimBLE internals.
//
// Important: This function reads the current global state and copies it into `out`.
bool webui_get_ble_snapshot(WebuiBleSnapshot& out) {
  out.connected     = g_bleStatus.connected;
  out.mtu           = g_bleStatus.mtu;

  // Keep JSON compact: truncate counters to 32-bit here.
  // If you want exact long-running counters, change WebuiBleSnapshot to uint64_t.
  out.txBytes = (uint32_t)g_bleStatus.txBytes;
  out.rxBytes = (uint32_t)g_bleStatus.rxBytes;
  out.uart2bleDrops = g_bleStatus.uart2bleDrops;
  out.ble2uartDrops = g_bleStatus.ble2uartDrops;

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
  return true;
}
#endif

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

// ----------- FUNCTIONS DECLARATIONS -----------
// Declared here so setup() can call them before their definitions below.
static void setupBLE();
static void setupUART();
#if WEBUI_ENABLE
static void setupWiFiAndWeb();
#endif
static void task_uart_rx(void* arg);
static void task_ble_tx(void* arg);
static void task_uart_tx(void* arg);

// -------------------------------------------
// -------------- SETUP & LOOP ---------------
// -------------------------------------------

void setup() {
  // Debug serial over USB.
  Serial.begin(SERIAL_BAUD);

  // Give USB CDC + RTOS some time to settle (especially right after boot).
  vTaskDelay(pdMS_TO_TICKS(200));

  // Configure BLE server + characteristics + advertising.
  setupBLE();

  #if WEBUI_ENABLE
  // Configure HTTP routes / static assets for the status UI.
  // (Doing this before server.begin() is fine; it just registers handlers.)
  webui_begin(server, STA_DNS);

  // Connect to WiFi (STA) and start the HTTP server.
  setupWiFiAndWeb();
  #endif

  // Initialize NMEA parser module (optional).
  #if NMEA_ENABLE
   nmea_begin();
  #endif

  // Create StreamBuffers using static storage (no heap allocation for the buffers).
  // - UART->BLE buffer holds bytes read from Serial1 (GNSS output).
  // - BLE->UART buffer holds bytes written by phone to BLE RX characteristic.
  g_sb_uart2ble = xStreamBufferCreateStatic(
      SB_UART_TO_BLE_SIZE, SB_TRIGGER_LEVEL,
      g_sb_uart2ble_storage, &g_sb_uart2ble_struct);

  g_sb_ble2uart = xStreamBufferCreateStatic(
      SB_BLE_TO_UART_SIZE, SB_TRIGGER_LEVEL,
      g_sb_ble2uart_storage, &g_sb_ble2uart_struct);

  // If allocation fails, we cannot safely run. Halt here (infinite delay loop).
  if (!g_sb_uart2ble || !g_sb_ble2uart) {
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
  }

  // Configure the hardware UART to talk to GNSS.
  setupUART();

  // Create worker tasks.
  // We prioritize UART tasks slightly higher so RTCM bytes (from phone) can reach GNSS
  // quickly even if BLE notify or HTTP are busy.
  xTaskCreate(task_uart_rx, "uart_rx", 4096, nullptr, 3, nullptr);
  xTaskCreate(task_uart_tx, "uart_tx", 4096, nullptr, 3, nullptr);
  xTaskCreate(task_ble_tx,  "ble_tx",  4096, nullptr, 2, nullptr);
}

void loop() {
  #if WEBUI_ENABLE
  static unsigned long last_wifi_attempt = 0;
  if (WiFi.status() != WL_CONNECTED) {
    const unsigned long now = millis();
    if ((now - last_wifi_attempt) > 5000) {
      WiFi.reconnect();
      last_wifi_attempt = now;
    }
  }

  // WebServer is polled; it processes one client request per call.
  server.handleClient();
  #endif

  // Small delay yields CPU to other FreeRTOS tasks on ESP32 Arduino.
  delay(2);
}

// -------------------------------------------
// ---------------- FUNCTIONS ----------------
// -------------------------------------------

#if WEBUI_ENABLE
static void setupWiFiAndWeb() {
  // Station mode: connect to an existing access point / hotspot.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.setSleep(true);

  // Apply static IP configuration for the STA interface.
  // Order: local IP, gateway, subnet, DNS.
  WiFi.config(STA_IP, STA_GW, STA_SUBNET, STA_DNS);

  // Start connection attempt using SSID/PASS.
  WiFi.begin(STA_SSID, STA_PASS);

  // Wait up to 15 seconds for connection.
  // (If not connected, we still start the server; you can view status / retry logic elsewhere.)
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 15000) {
    delay(250);
  }

  // Start listening for HTTP requests.
  server.begin();
}
#endif

// ---------------- Setup BLE ----------------
static void setupBLE() {
  // Initialize NimBLE and set the device name used in advertising.
  NimBLEDevice::init(DEVICE_NAME);

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

// ---------------- Setup UART ----------------
static void setupUART() {
  // ESP32-C3 Arduino:
  // - Serial  = USB CDC (debug)
  // - Serial1 = hardware UART
  //
  // Configure Serial1 to talk to the GNSS module at GNSS_BAUD using 8N1.
  Serial1.begin(GNSS_BAUD, SERIAL_8N1, PIN_GNSS_RX, PIN_GNSS_TX);
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

      // Push bytes into UART->BLE buffer.
      // Timeout 0 = non-blocking; if full, bytes are dropped (best-effort).
      if (g_sb_uart2ble) {        size_t sent = xStreamBufferSend(g_sb_uart2ble, tmp, (size_t)n, 0);
        if (sent < (size_t)n) {
          g_bleStatus.uart2bleDrops += (uint32_t)((size_t)n - sent);
        }
      }
    }
  }
}

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

// UART TX task:
// Pulls bytes from BLE->UART buffer (typically RTCM corrections) and writes to GNSS (Serial1).
static void task_uart_tx(void* arg) {
  (void)arg;

  // Scratch buffer for UART writes.
  uint8_t tmp[UART_CHUNK];

  for (;;) {
    // Wait for up to 50 ms for inbound bytes (phone -> BLE -> buffer).
    size_t got = 0;
    if (g_sb_ble2uart) {
      got = xStreamBufferReceive(g_sb_ble2uart, tmp, sizeof(tmp), pdMS_TO_TICKS(50));
    }

    // If we received bytes, forward them to GNSS.
    if (got > 0) {
      Serial1.write(tmp, got);

      // Small delay yields to avoid starving other tasks in tight loops.
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}
