/*
  ESP32-C3 (Arduino) BLE <-> UART bridge for UM980
  + WiFi STA + WebServer for Status

  - BLE (NUS) using NimBLE-Arduino (h2zero)
  - FreeRTOS StreamBuffers as ring buffers
  - iPhone compatible: subscribe callback tracks notify enable
  - Backpressure aware: does not drain UART->BLE faster than notify succeeds

  Direction:
    UM980 -> iPhone : UART RX -> stream buffer -> BLE notify (NUS TX)
    iPhone -> UM980 : BLE write (NUS RX) -> stream buffer -> UART TX

  Pins (ESP32-C3):
    GPIO20 = RX  (connect to UM980 TX)
    GPIO21 = TX  (connect to UM980 RX)

  Notes:
    - Streams raw bytes (NMEA + any other UM980 output)
    - RTCM bursts are buffered; when buffers fill, new bytes are dropped (no counters)
*/

#include <Arduino.h>

// ---- BLE ----
#include <NimBLEDevice.h>

// ---- (WiFi + WebServer) ----
#include <WiFi.h>
#include <WebServer.h>
#include "web_ui.h"

// ---- NMEA ----
#if NMEA_ENABLE
#include "nmea_gps.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"

// ---------------- STA config (Hotspot) ----------------
static const char* STA_SSID = "64NDPVIWJCMG7RUZ9392";
static const char* STA_PASS = "azerty1234";
static const IPAddress STA_IP     (172, 20, 10, 2);
static const IPAddress STA_GW     (172, 20, 10, 1);
static const IPAddress STA_SUBNET (255, 255, 255, 240);
static const IPAddress STA_DNS    (172, 20, 10, 1);

// ---- Webserver ----
WebServer server(80);

// ---------------- BT ----------------
static const char DEVICE_NAME[] = "UM980-BLE";    // Device name
static const uint16_t BLE_MTU = 185;              // Request a larger MTU

// ---------------- UART ----------------
static const int PIN_UM980_RX = 20;      // ESP32 RX  (UM980 TX)
static const int PIN_UM980_TX = 21;      // ESP32 TX  (UM980 RX)
static const uint32_t UM980_BAUD = 115200; // BAUD RATE UM980

// ---------------- BLE (NUS UUIDs) ----------------
static NimBLEUUID NUS_SERVICE_UUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
static NimBLEUUID NUS_RX_UUID     ("6E400002-B5A3-F393-E0A9-E50E24DCCA9E"); // write from phone
static NimBLEUUID NUS_TX_UUID     ("6E400003-B5A3-F393-E0A9-E50E24DCCA9E"); // notify to phone

// ---------------- Tunables ----------------
// Defines the maximum number of bytes to send in a single BLE notification.
static const size_t BLE_NOTIFY_CHUNK = 120;
// Defines the maximum number of bytes transferred per loop iteration.
static const size_t UART_CHUNK = 256;

// Ring buffer sizes
static const size_t SB_UART_TO_BLE_SIZE = 4096;   // NMEA buffer
static const size_t SB_BLE_TO_UART_SIZE = 16384;  // RTCM buffer (bursty; increase if RAM allows)

// StreamBuffer trigger level (unblocks receivers once >= this many bytes exist)
static const size_t SB_TRIGGER_LEVEL = 1;

// Backpressure pacing
static const TickType_t BLE_TX_WAIT_TICKS = pdMS_TO_TICKS(50); // wait for UART bytes
static const TickType_t BLE_OK_DELAY      = pdMS_TO_TICKS(1);  // delay after successful notify
static const TickType_t BLE_FAIL_DELAY    = pdMS_TO_TICKS(15); // delay after failed notify

// ---------------- Globals ----------------
static NimBLEServer*         g_server    = nullptr;
static NimBLECharacteristic* g_txChar    = nullptr;
static bool                  g_connected = false;
static bool                  g_notifyEn  = false;

// StreamBuffers (static)
static StaticStreamBuffer_t  g_sb_uart2ble_struct;
static StaticStreamBuffer_t  g_sb_ble2uart_struct;
static uint8_t               g_sb_uart2ble_storage[SB_UART_TO_BLE_SIZE];
static uint8_t               g_sb_ble2uart_storage[SB_BLE_TO_UART_SIZE];
static StreamBufferHandle_t  g_sb_uart2ble = nullptr;
static StreamBufferHandle_t  g_sb_ble2uart = nullptr;

// ========================= BLE STATUS (BLOCK) =========================
struct BleStatus {
  volatile bool     connected      = false;

  volatile uint16_t mtu            = 0;

  volatile uint64_t txBytes        = 0;       // bytes successfully notified
  volatile uint64_t rxBytes        = 0;       // bytes received from phone (writes)

  void resetCounters() {
    txBytes = 0; rxBytes = 0;
  }
};

static BleStatus g_bleStatus;

// ======================= END BLE STATUS (BLOCK) =======================

// ---- Snapshot getter for web_ui.cpp (declared in web_ui.h) ----
bool webui_get_ble_snapshot(WebuiBleSnapshot& out) {
  out.connected     = g_bleStatus.connected;

  out.mtu  = g_bleStatus.mtu;

  // Keep JSON compact: truncate counters to 32-bit here (you can switch to uint64_t if desired)
  out.txBytes = (uint32_t)g_bleStatus.txBytes;
  out.rxBytes = (uint32_t)g_bleStatus.rxBytes;

  return true;
}

#if NMEA_ENABLE
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
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    (void)pServer; (void)connInfo;
    g_connected = true;
    g_notifyEn  = false; // will be set by subscribe callback

    // ---- status hook ----
    g_bleStatus.connected = true;
    g_bleStatus.mtu = connInfo.getMTU();

    Serial.println("[BLE] Connected");
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    (void)pServer; (void)connInfo; (void)reason;
    g_connected = false;
    g_notifyEn  = false;

    // Flush pending NMEA so next connect is "live"
    if (g_sb_uart2ble) xStreamBufferReset(g_sb_uart2ble);

    // ---- status hook ----
    g_bleStatus.connected = false;
    g_bleStatus.mtu = 0;

    Serial.println("[BLE] Disconnected - advertising again");
    NimBLEDevice::startAdvertising();
  }
};

class TxCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* pCharacteristic,
                   NimBLEConnInfo& connInfo,
                   uint16_t subValue) override {
    (void)pCharacteristic; (void)connInfo;
    // bit0 = notifications enabled
    g_notifyEn = (subValue & 0x0001);

    Serial.print("[BLE] Notify ");
    Serial.println(g_notifyEn ? "ENABLED" : "DISABLED");

    // When notifications become enabled, drop any backlog so stream starts live
    if (g_notifyEn && g_sb_uart2ble) xStreamBufferReset(g_sb_uart2ble);
  }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    (void)connInfo;

    const std::string& v = pCharacteristic->getValue();
    if (v.empty() || !g_sb_ble2uart) return;

    // ---- status hook ----
    g_bleStatus.rxBytes += v.size();

    // Best-effort push RTCM bytes into BLE->UART buffer (drop if full)
    xStreamBufferSend(g_sb_ble2uart, v.data(), v.size(), 0);
  }
};

// ----------- FUNCTIONS DECLARATIONS -----------
static void setupBLE();
static void setupUART();
static void setupWiFiAndWeb();
static void task_uart_rx(void* arg);
static void task_ble_tx(void* arg);
static void task_uart_tx(void* arg);

// -------------------------------------------
// -------------- SETUP & LOOP ---------------
// -------------------------------------------

void setup() {
  Serial.begin(UM980_BAUD);
  vTaskDelay(pdMS_TO_TICKS(200));
  Serial.println("\n--- ESP32-C3 UM980 BLE Bridge + STA WebServer ---");

  // ---- (web routes before server.begin is fine) ----
  webui_begin(server, STA_DNS);

  // ---- (connect STA + start HTTP server) ----
  setupWiFiAndWeb();

  // ---- (NMEA) ----
  #if NMEA_ENABLE
   nmea_begin();
   #endif

  // Create StreamBuffers (static)
  g_sb_uart2ble = xStreamBufferCreateStatic(
      SB_UART_TO_BLE_SIZE, SB_TRIGGER_LEVEL,
      g_sb_uart2ble_storage, &g_sb_uart2ble_struct);

  g_sb_ble2uart = xStreamBufferCreateStatic(
      SB_BLE_TO_UART_SIZE, SB_TRIGGER_LEVEL,
      g_sb_ble2uart_storage, &g_sb_ble2uart_struct);

  if (!g_sb_uart2ble || !g_sb_ble2uart) {
    Serial.println("[ERR] StreamBuffer create failed (RAM too low?)");
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
  }

  setupUART();
  setupBLE();

  // Tasks: prioritize UART a bit so RTCM isn't delayed under load
  xTaskCreate(task_uart_rx, "uart_rx", 4096, nullptr, 3, nullptr);
  xTaskCreate(task_uart_tx, "uart_tx", 4096, nullptr, 3, nullptr);
  xTaskCreate(task_ble_tx,  "ble_tx",  4096, nullptr, 2, nullptr);

  Serial.println("[RTOS] Tasks started");
}

void loop() {
  server.handleClient();
  delay(2); // yields to RTOS on ESP32 Arduino
}

// -------------------------------------------
// ---------------- FUNCTIONS ----------------
// -------------------------------------------

static void setupWiFiAndWeb() {
  WiFi.mode(WIFI_STA);
  WiFi.config(STA_IP, STA_GW, STA_SUBNET, STA_DNS);
  WiFi.begin(STA_SSID, STA_PASS);

  Serial.print("STA SSID: "); Serial.println(STA_SSID);
  Serial.print("Connecting");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 15000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected.");
    Serial.print("STA IP:   "); Serial.println(WiFi.localIP());
    Serial.print("STA GW:   "); Serial.println(WiFi.gatewayIP());
    Serial.print("STA MASK: "); Serial.println(WiFi.subnetMask());
    Serial.print("RSSI:     "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
  } else {
    Serial.println("WiFi NOT connected (timeout).");
  }

  server.begin();
  Serial.println("HTTP server started on port 80");
}

// ---------------- Setup BLE ----------------
static void setupBLE() {
  NimBLEDevice::init(DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P6);
  NimBLEDevice::setMTU(BLE_MTU);

  g_server = NimBLEDevice::createServer();
  g_server->setCallbacks(new ServerCallbacks());

  NimBLEService* svc = g_server->createService(NUS_SERVICE_UUID);

  // TX (notify)
  g_txChar = svc->createCharacteristic(NUS_TX_UUID, NIMBLE_PROPERTY::NOTIFY);
  g_txChar->setCallbacks(new TxCallbacks());

  // RX (write / write without response)
  NimBLECharacteristic* rxChar =
      svc->createCharacteristic(NUS_RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rxChar->setCallbacks(new RxCallbacks());

  svc->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SERVICE_UUID);
  adv->enableScanResponse(true);
  adv->start();

  Serial.println("[BLE] Advertising started");
}

// ---------------- Setup UART ----------------
static void setupUART() {
  // C3: Serial is USB CDC (debug), Serial1 is HW UART
  Serial1.begin(UM980_BAUD, SERIAL_8N1, PIN_UM980_RX, PIN_UM980_TX);
  Serial.println("[UART] Serial1 started");
}

// UART RX task: reads bytes from UM980 and pushes into UART->BLE buffer
static void task_uart_rx(void* arg) {
  (void)arg;
  uint8_t tmp[UART_CHUNK];

  for (;;) {
    int avail = Serial1.available();
    if (avail <= 0) {
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }

    // int n = Serial1.readBytes(tmp, (size_t)min(avail, (int)sizeof(tmp)));
    // if (n > 0 && g_sb_uart2ble) {
    //   // Best-effort push; drop if full
    //   xStreamBufferSend(g_sb_uart2ble, tmp, (size_t)n, 0);
    // }
    
    int n = Serial1.readBytes(tmp, (size_t)min(avail, (int)sizeof(tmp)));
    if (n > 0) {
      // Feed NMEA parser from the same bytes
      #if NMEA_ENABLE
      nmea_feed_bytes(tmp, (size_t)n, millis());
      #endif

      // Keep existing UART->BLE buffering unchanged
      if (g_sb_uart2ble) {
        xStreamBufferSend(g_sb_uart2ble, tmp, (size_t)n, 0);
      }
    }
  }
}

// BLE TX task: pulls from UART->BLE buffer and notifies to iPhone when subscribed
static void task_ble_tx(void* arg) {
  (void)arg;
  uint8_t out[BLE_NOTIFY_CHUNK];

  for (;;) {
    // If not connected or not subscribed, don't drain the UART buffer endlessly.
    if (!(g_connected && g_notifyEn && g_txChar)) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    // Wait for UART bytes
    size_t got = 0;
    if (g_sb_uart2ble) {
      got = xStreamBufferReceive(g_sb_uart2ble, out, sizeof(out), BLE_TX_WAIT_TICKS);
    }
    if (got == 0) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    // Notify with backpressure handling
    g_txChar->setValue(out, got);
    bool ok = g_txChar->notify();

    // ---- status hook ----
    if (ok) { g_bleStatus.txBytes += got; }

    if (ok) {
      vTaskDelay(BLE_OK_DELAY);
    } else {
      vTaskDelay(BLE_FAIL_DELAY);
    }
  }
}

// UART TX task: pulls from BLE->UART buffer (RTCM) and writes to UM980
static void task_uart_tx(void* arg) {
  (void)arg;
  uint8_t tmp[UART_CHUNK];

  for (;;) {
    size_t got = 0;
    if (g_sb_ble2uart) {
      got = xStreamBufferReceive(g_sb_ble2uart, tmp, sizeof(tmp), pdMS_TO_TICKS(50));
    }

    if (got > 0) {
      Serial1.write(tmp, got);
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}
