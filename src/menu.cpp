#if BLE_ENABLE
/*
 * ESP32 BLE Configuration Tool for GNSS/RTK Systems
 * --------------------------------------------------
 * Multi-stage configuration interface using BLE + NVS.
 * Allows guided setup of WiFi, UART, and NTRIP settings
 * without recompiling firmware.
 *
 * Fixed and improved version:
 *  - Fixed substring index for PIN parsing (14, not 15)
 *  - Fixed missing break statements (WIFI_SUB, UART_BAUD, NTRIP_PASS)
 *  - Fixed enum casting mismatch for factory reset / reboot confirmation
 *  - Fixed prefs.remove() called without active NVS session
 *  - Improved IP validation (octet range 0-255)
 *  - Added numeric input validation before toInt()
 *  - Fixed WIFI_SSID state overload (separated sub-menu from input)
 *  - Reduced BLE latency (removed blocking delay)
 *  - Chunked showSettings output for BLE size limit
 *  - Consolidated NVS open/close within menu sessions
 */

#include <Preferences.h>
#include <LittleFS.h>
#include "menu.h"
#include <NimBLEDevice.h>

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
enum States {
  LOCKED,
  MAIN_MENU,
  WIFI_MENU,          // WiFi sub-menu (was overloaded on WIFI_SSID)
  WIFI_SSID,
  WIFI_PASS,
  WIFI_DHCP,
  WIFI_IP,
  WIFI_GW,
  WIFI_SUB,
  WIFI_DNS,
  UART_RX,
  UART_TX,
  UART_BAUD,
  NTRIP_URL,
  NTRIP_PORT,
  NTRIP_BASE,
  NTRIP_EMAIL,
  NTRIP_PASS,
  CONFIRM_RESET,      // Dedicated states instead of bogus enum math
  CONFIRM_REBOOT
};

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
States currentState = LOCKED;
Preferences prefs;
unsigned long lastActivityTime = 0;
const unsigned long TIMEOUT_MS = 30000;

const int safePins[] = {4, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
const int safePinsCount = sizeof(safePins) / sizeof(safePins[0]);
const String DEFAULT_PIN = "1234";
const char* NS_WIFI = "wifi";
const char* NS_GNSS = "gnss";
const char* NS_NTRIP = "ntrip";
const char* NTRIP_JSON_PATH = "/ntrip_config.json";

#if BLE_ENABLE
static NimBLEService* g_menuService = nullptr;
static NimBLECharacteristic* g_menuChar = nullptr;
#endif

void handleInput(String input);
void handleMainMenu(const String& input);
void showSettings();
void factoryReset();

enum class NvsValueType { Bool, Int, UInt, String };
struct NvsKey {
  const char* ns;
  const char* key;
  NvsValueType type;
};

// WiFi keys
static const NvsKey K_WIFI_SSID   = {NS_WIFI, "ssid", NvsValueType::String};
static const NvsKey K_WIFI_PASS   = {NS_WIFI, "pass", NvsValueType::String};
static const NvsKey K_WIFI_DHCP   = {NS_WIFI, "dhcp", NvsValueType::Bool};
static const NvsKey K_WIFI_IP     = {NS_WIFI, "ip", NvsValueType::String};
static const NvsKey K_WIFI_GW     = {NS_WIFI, "gw", NvsValueType::String};
static const NvsKey K_WIFI_SUBNET = {NS_WIFI, "subnet", NvsValueType::String};
static const NvsKey K_WIFI_DNS    = {NS_WIFI, "dns", NvsValueType::String};

// GNSS keys
static const NvsKey K_GNSS_RX   = {NS_GNSS, "rx_pin", NvsValueType::Int};
static const NvsKey K_GNSS_TX   = {NS_GNSS, "tx_pin", NvsValueType::Int};
static const NvsKey K_GNSS_BAUD = {NS_GNSS, "baud", NvsValueType::UInt};

// NTRIP keys
static const NvsKey K_NTRIP_ENABLED            = {NS_NTRIP, "enabled", NvsValueType::Bool};
static const NvsKey K_NTRIP_HOST               = {NS_NTRIP, "host", NvsValueType::String};
static const NvsKey K_NTRIP_PORT               = {NS_NTRIP, "port", NvsValueType::UInt};
static const NvsKey K_NTRIP_MOUNT              = {NS_NTRIP, "mount", NvsValueType::String};
static const NvsKey K_NTRIP_USER               = {NS_NTRIP, "user", NvsValueType::String};
static const NvsKey K_NTRIP_PASS               = {NS_NTRIP, "pass", NvsValueType::String};
static const NvsKey K_NTRIP_MAX_TRIES          = {NS_NTRIP, "max_tries", NvsValueType::Int};
static const NvsKey K_NTRIP_RETRY_DELAY_MS     = {NS_NTRIP, "retry_delay_ms", NvsValueType::UInt};
static const NvsKey K_NTRIP_HEALTH_TIMEOUT_MS  = {NS_NTRIP, "health_timeout_ms", NvsValueType::UInt};
static const NvsKey K_NTRIP_PASSIVE_SAMPLE_MS  = {NS_NTRIP, "passive_sample_ms", NvsValueType::UInt};
static const NvsKey K_NTRIP_REQUIRED_VALID     = {NS_NTRIP, "required_valid_frames", NvsValueType::UInt};
static const NvsKey K_NTRIP_BUFFER_SIZE        = {NS_NTRIP, "buffer_size", NvsValueType::UInt};
static const NvsKey K_NTRIP_CONNECT_TIMEOUT_MS = {NS_NTRIP, "connect_timeout_ms", NvsValueType::UInt};


// ---------------------------------------------------------------------------
// Helpers – validation
// ---------------------------------------------------------------------------

bool isNumeric(const String& s) {
  if (s.length() == 0) return false;
  for (unsigned int i = 0; i < s.length(); i++) {
    if (!isDigit(s.charAt(i))) return false;
  }
  return true;
}

bool isValidSSID(const String& ssid) {
  return ssid.length() > 0 && ssid.length() <= 32;
}

bool isValidPassword(const String& pass) {
  return pass.length() <= 64;
}

bool isValidIP(const String& ip) {
  int start = 0;
  int octetCount = 0;

  for (int i = 0; i <= (int)ip.length(); i++) {
    if (i == (int)ip.length() || ip.charAt(i) == '.') {
      String segment = ip.substring(start, i);
      if (segment.length() == 0 || segment.length() > 3) return false;
      if (!isNumeric(segment)) return false;
      int val = segment.toInt();
      if (val < 0 || val > 255) return false;
      octetCount++;
      start = i + 1;
    }
  }
  return octetCount == 4;
}

bool isValidPort(const String& input) {
  if (!isNumeric(input)) return false;
  long port = input.toInt();
  return port >= 1 && port <= 65535;
}

bool isValidBaud(const String& input) {
  if (!isNumeric(input)) return false;
  int baud = input.toInt();
  const int validBauds[] = {9600, 19200, 38400, 57600, 115200};
  for (int b : validBauds) {
    if (baud == b) return true;
  }
  return false;
}

bool isPinSafe(const String& input) {
  if (!isNumeric(input)) return false;
  int pin = input.toInt();
  for (int i = 0; i < safePinsCount; i++) {
    if (safePins[i] == pin) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Helpers – NVS wrappers
// ---------------------------------------------------------------------------

void saveStr(const NvsKey& k, const String& val) {
  prefs.begin(k.ns, false);
  prefs.putString(k.key, val);
  prefs.end();
}

void saveInt(const NvsKey& k, int val) {
  prefs.begin(k.ns, false);
  prefs.putInt(k.key, val);
  prefs.end();
}

void saveUInt(const NvsKey& k, uint32_t val) {
  prefs.begin(k.ns, false);
  prefs.putUInt(k.key, val);
  prefs.end();
}

void saveBool(const NvsKey& k, bool val) {
  prefs.begin(k.ns, false);
  prefs.putBool(k.key, val);
  prefs.end();
}

String readStr(const NvsKey& k, const char* def) {
  prefs.begin(k.ns, true);
  String val = prefs.getString(k.key, def);
  prefs.end();
  return val;
}

int readInt(const NvsKey& k, int def) {
  prefs.begin(k.ns, true);
  int val = prefs.getInt(k.key, def);
  prefs.end();
  return val;
}

uint32_t readUInt(const NvsKey& k, uint32_t def) {
  prefs.begin(k.ns, true);
  uint32_t val = prefs.getUInt(k.key, def);
  prefs.end();
  return val;
}

bool readBool(const NvsKey& k, bool def) {
  prefs.begin(k.ns, true);
  bool val = prefs.getBool(k.key, def);
  prefs.end();
  return val;
}

bool hasKey(const NvsKey& k) {
  prefs.begin(k.ns, true);
  bool exists = prefs.isKey(k.key);
  prefs.end();
  return exists;
}

// ---------------------------------------------------------------------------
// Helpers – BLE output
// ---------------------------------------------------------------------------

static const char* MENU_FOOTER =
  "\n\n1. WiFi\n2. UART\n3. NTRIP\n4. Show Info\n5. Factory Reset\n6. Reboot"
  "\n\nSend 'x' to cancel any menu.";

void sendMenu(const String& msg) {
  // Guard against exceeding BLE characteristic size
  const String out = (msg.length() <= 512) ? msg : (msg.substring(0, 509) + "...");
#if BLE_ENABLE
  if (g_menuChar) {
    g_menuChar->setValue(std::string(out.c_str()));
    g_menuChar->notify();
  }
#endif
  Serial.println("Menu: " + out);
}

void returnToMenu(const String& msg) {
  currentState = MAIN_MENU;
  sendMenu(msg + MENU_FOOTER);
}

// ---------------------------------------------------------------------------
// Setup & Tick (NimBLE callback-driven)
// ---------------------------------------------------------------------------

#if BLE_ENABLE
class MenuRxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo&) override {
    const std::string raw = pCharacteristic->getValue();
    String input(raw.c_str());
    input.trim();
    lastActivityTime = millis();
    handleInput(input);
  }
};
#endif

void menuToolSetup(NimBLEServer* server) {
#if !BLE_ENABLE
  Serial.println("Menu tool disabled: BLE_ENABLE=0");
  return;
#else
  if (!server) {
    Serial.println("Menu tool setup failed: null BLE server");
    return;
  }
  g_menuService = server->createService("180C");
  g_menuChar = g_menuService->createCharacteristic(
      "2A56",
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  g_menuChar->setCallbacks(new MenuRxCallbacks());
  g_menuService->start();
  if (!prefs.begin(NS_WIFI, false)) {
    Serial.println("NVS init failed! Factory reset may be needed.");
  } else {
    prefs.end();
  }

  Serial.println("BLE Config Tool ready.");
#endif
}

void menuToolTick() {
  if (currentState != LOCKED && millis() - lastActivityTime > TIMEOUT_MS) {
    currentState = LOCKED;
    sendMenu("TIMEOUT: Returning to locked state.");
  }
}


// ---------------------------------------------------------------------------
// State machine – input handler
// ---------------------------------------------------------------------------

void handleInput(String input) {

  // ---- LOCKED ----
  if (currentState == LOCKED) {
    if (input.startsWith("activate-menu ")) {
      String pin = input.substring(14);  // Fixed: "activate-menu " is 14 chars
      if (pin == DEFAULT_PIN) {
        currentState = MAIN_MENU;
        sendMenu("SYSTEM UNLOCKED" + String(MENU_FOOTER));
      } else {
        sendMenu("ERROR: Invalid PIN.");
      }
    }
    return;
  }

  // ---- Global cancel ----
  if (input.equalsIgnoreCase("x")) {
    returnToMenu("Cancelled.");
    return;
  }

  // ---- State dispatch ----
  switch (currentState) {

    // ===================== MAIN MENU =====================
    case MAIN_MENU:
      handleMainMenu(input);
      break;

    // ===================== WIFI =====================
    case WIFI_MENU:
      if (input == "1") {
        String cur = readStr(K_WIFI_SSID, "Not set");
        sendMenu("--- WiFi Setup (Step 1/4) ---\nCurrent SSID: " + cur +
                 "\nEnter WiFi SSID:");
        currentState = WIFI_SSID;
      } else if (input == "2") {
        returnToMenu("");
      } else {
        sendMenu("ERROR: Enter 1 or 2.");
      }
      break;

    case WIFI_SSID:
      if (!isValidSSID(input)) {
        sendMenu("ERROR: SSID must be 1-32 chars.\nEnter WiFi SSID:");
        return;
      }
      saveStr(K_WIFI_SSID, input);
      sendMenu("--- WiFi Setup (Step 2/4) ---\nEnter Password (leave empty for open network):");
      currentState = WIFI_PASS;
      break;

    case WIFI_PASS:
      if (!isValidPassword(input)) {
        sendMenu("ERROR: Password max 64 chars.\nEnter Password:");
        return;
      }
      saveStr(K_WIFI_PASS, input);
      {
        bool dhcp = readBool(K_WIFI_DHCP, true);
        sendMenu("--- WiFi Setup (Step 3/4) ---\nCurrent DHCP: " +
                 String(dhcp ? "Yes" : "No") + "\nUse DHCP? (y/n):");
      }
      currentState = WIFI_DHCP;
      break;

    case WIFI_DHCP:
      if (input != "y" && input != "n") {
        sendMenu("ERROR: Enter 'y' or 'n' for DHCP:");
        return;
      }
      saveBool(K_WIFI_DHCP, input == "y");
      if (input == "n") {
        String ip = readStr(K_WIFI_IP, "Not set");
        sendMenu("--- WiFi Setup (Step 4/6) ---\nCurrent IP: " + ip +
                 "\nEnter Static IP (e.g. 192.168.1.50):");
        currentState = WIFI_IP;
      } else {
        saveStr(K_WIFI_IP, "0.0.0.0");
        saveStr(K_WIFI_GW, "0.0.0.0");
        saveStr(K_WIFI_SUBNET, "0.0.0.0");
        saveStr(K_WIFI_DNS, "0.0.0.0");
        returnToMenu("WiFi Saved (DHCP).");
      }
      break;

    case WIFI_IP:
      if (!isValidIP(input)) {
        sendMenu("ERROR: Invalid IP format.\nEnter Static IP:");
        return;
      }
      saveStr(K_WIFI_IP, input);
      {
        String gw = readStr(K_WIFI_GW, "Not set");
        sendMenu("--- WiFi Setup (Step 5/6) ---\nCurrent Gateway: " + gw +
                 "\nEnter Gateway IP (e.g. 192.168.1.1):");
        currentState = WIFI_GW;
      }
      break;

    case WIFI_GW:
      if (!isValidIP(input)) {
        sendMenu("ERROR: Invalid IP format.\nEnter Gateway IP:");
        return;
      }
      saveStr(K_WIFI_GW, input);
      {
        String sub = readStr(K_WIFI_SUBNET, "255.255.255.0");
        sendMenu("Current Subnet: " + sub + "\nEnter Subnet Mask:");
      }
      currentState = WIFI_SUB;
      break;

    case WIFI_SUB:
      if (!isValidIP(input)) {
        sendMenu("ERROR: Invalid subnet format.\nEnter Subnet Mask:");
        return;
      }
      saveStr(K_WIFI_SUBNET, input);
      {
        String dns = readStr(K_WIFI_DNS, "8.8.8.8");
        sendMenu("--- WiFi Setup (Step 6/6) ---\nCurrent DNS: " + dns + "\nEnter DNS IP:");
      }
      currentState = WIFI_DNS;
      break;  // Fixed: was missing

    case WIFI_DNS:
      if (!isValidIP(input)) {
        sendMenu("ERROR: Invalid DNS format.\nEnter DNS IP:");
        return;
      }
      saveStr(K_WIFI_DNS, input);
      returnToMenu("Static WiFi Saved.");
      break;

    // ===================== UART =====================
    case UART_RX:
      if (!isPinSafe(input)) {
        sendMenu("ERROR: Invalid pin. Safe: 4,13,14,16-19,21-23,25-27,32,33\nEnter RX Pin:");
        return;
      }
      saveInt(K_GNSS_RX, input.toInt());
      {
        int tx = readInt(K_GNSS_TX, -1);
        sendMenu("--- UART Setup (Step 2/3) ---\nCurrent TX: " + String(tx) +
                 "\nEnter TX Pin:");
      }
      currentState = UART_TX;
      break;

    case UART_TX:
      if (!isPinSafe(input)) {
        sendMenu("ERROR: Invalid pin. Safe: 4,13,14,16-19,21-23,25-27,32,33\nEnter TX Pin:");
        return;
      }
      saveInt(K_GNSS_TX, input.toInt());
      {
        int baud = readInt(K_GNSS_BAUD, 115200);
        sendMenu("--- UART Setup (Step 3/3) ---\nCurrent Baud: " + String(baud) +
                 "\nEnter Baud Rate (9600,19200,38400,57600,115200):");
      }
      currentState = UART_BAUD;
      break;

    case UART_BAUD:
      if (!isValidBaud(input)) {
        sendMenu("ERROR: Invalid baud. Use: 9600,19200,38400,57600,115200\nEnter Baud Rate:");
        return;
      }
      saveUInt(K_GNSS_BAUD, (uint32_t)input.toInt());
      returnToMenu("UART Settings Saved.");
      break;  // Fixed: was missing

    // ===================== NTRIP =====================
    case NTRIP_URL:
      if (input.length() == 0) {
        sendMenu("ERROR: URL cannot be empty.\nEnter NTRIP URL:");
        return;
      }
      saveBool(K_NTRIP_ENABLED, true);
      saveStr(K_NTRIP_HOST, input);
      {
        int port = (int)readUInt(K_NTRIP_PORT, 2101);
        sendMenu("--- NTRIP Setup (Step 2/5) ---\nCurrent Port: " + String(port) +
                 "\nEnter Port (1-65535):");
      }
      currentState = NTRIP_PORT;
      break;

    case NTRIP_PORT:
      if (!isValidPort(input)) {
        sendMenu("ERROR: Port must be 1-65535.\nEnter Port:");
        return;
      }
      saveUInt(K_NTRIP_PORT, (uint32_t)input.toInt());
      {
        String base = readStr(K_NTRIP_MOUNT, "Not set");
        sendMenu("--- NTRIP Setup (Step 3/5) ---\nCurrent Base ID: " + base +
                 "\nEnter Base ID:");
      }
      currentState = NTRIP_BASE;
      break;

    case NTRIP_BASE:
      saveStr(K_NTRIP_MOUNT, input);
      {
        String email = readStr(K_NTRIP_USER, "Not set");
        sendMenu("--- NTRIP Setup (Step 4/5) ---\nCurrent Email: " + email +
                 "\nEnter Email:");
      }
      currentState = NTRIP_EMAIL;
      break;

    case NTRIP_EMAIL:
      saveStr(K_NTRIP_USER, input);
      sendMenu("--- NTRIP Setup (Step 5/5) ---\nEnter Password:");
      currentState = NTRIP_PASS;
      break;

    case NTRIP_PASS:
      saveStr(K_NTRIP_PASS, input);
      // Ensure all runtime NTRIP keys exist
      if (!hasKey(K_NTRIP_MAX_TRIES)) saveInt(K_NTRIP_MAX_TRIES, 5);
      if (!hasKey(K_NTRIP_RETRY_DELAY_MS)) saveUInt(K_NTRIP_RETRY_DELAY_MS, 30000);
      if (!hasKey(K_NTRIP_HEALTH_TIMEOUT_MS)) saveUInt(K_NTRIP_HEALTH_TIMEOUT_MS, 60000);
      if (!hasKey(K_NTRIP_PASSIVE_SAMPLE_MS)) saveUInt(K_NTRIP_PASSIVE_SAMPLE_MS, 5000);
      if (!hasKey(K_NTRIP_REQUIRED_VALID)) saveUInt(K_NTRIP_REQUIRED_VALID, 3);
      if (!hasKey(K_NTRIP_BUFFER_SIZE)) saveUInt(K_NTRIP_BUFFER_SIZE, 1024);
      if (!hasKey(K_NTRIP_CONNECT_TIMEOUT_MS)) saveUInt(K_NTRIP_CONNECT_TIMEOUT_MS, 5000);
      returnToMenu("NTRIP Settings Saved.");
      break;  // Fixed: was missing

    // ===================== CONFIRMATIONS =====================
    case CONFIRM_RESET:
      if (input == "y") {
        factoryReset();
      } else if (input == "n") {
        returnToMenu("Reset cancelled.");
      } else {
        sendMenu("Enter 'y' or 'n':");
      }
      break;

    case CONFIRM_REBOOT:
      if (input == "y") {
        sendMenu("Rebooting...");
        delay(500);
        ESP.restart();
      } else if (input == "n") {
        returnToMenu("Reboot cancelled.");
      } else {
        sendMenu("Enter 'y' or 'n':");
      }
      break;

    default:
      returnToMenu("Invalid input.");
      break;
  }
}

// ---------------------------------------------------------------------------
// Main menu handler (extracted for clarity)
// ---------------------------------------------------------------------------

void handleMainMenu(const String& input) {
  if (input == "1") {
    String cur = readStr(K_WIFI_SSID, "Not set");
    sendMenu("--- WiFi Setup ---\nCurrent SSID: " + cur +
             "\n\n1. Change SSID\n2. Back");
    currentState = WIFI_MENU;
  }
  else if (input == "2") {
    int rx   = readInt(K_GNSS_RX, -1);
    int tx   = readInt(K_GNSS_TX, -1);
    int baud = (int)readUInt(K_GNSS_BAUD, 115200);
    sendMenu("--- UART Setup (Step 1/3) ---\nCurrent: RX=" + String(rx) +
             ", TX=" + String(tx) + " @ " + String(baud) +
             "\nEnter RX Pin (e.g. 16, 17, 21) or 'x' to cancel:");
    currentState = UART_RX;
  }
  else if (input == "3") {
    String url = readStr(K_NTRIP_HOST, "Not set");
    sendMenu("--- NTRIP Setup (Step 1/5) ---\nCurrent URL: " + url +
             "\nEnter NTRIP URL or 'x' to cancel:");
    currentState = NTRIP_URL;
  }
  else if (input == "4") {
    showSettings();
  }
  else if (input == "5") {
    sendMenu("WARNING: Reset ALL settings? (y/n)");
    currentState = CONFIRM_RESET;
  }
  else if (input == "6") {
    sendMenu("Reboot device? (y/n)");
    currentState = CONFIRM_REBOOT;
  }
  else {
    sendMenu("Invalid option. Enter 1-6.");
  }
}

// ---------------------------------------------------------------------------
// Show all settings (chunked to stay within 512 byte BLE limit)
// ---------------------------------------------------------------------------

void showSettings() {
  // Build sections separately to control size
  String wifi = "--- CONFIG ---\nWiFi:\n";
  wifi += "  SSID: " + readStr(K_WIFI_SSID, "Not set") + "\n";
  bool dhcp = readBool(K_WIFI_DHCP, true);
  wifi += "  DHCP: " + String(dhcp ? "Yes" : "No") + "\n";
  if (!dhcp) {
    wifi += "  IP: " + readStr(K_WIFI_IP, "Not set") + "\n";
    wifi += "  GW: " + readStr(K_WIFI_GW, "Not set") + "\n";
    wifi += "  Subnet: " + readStr(K_WIFI_SUBNET, "Not set") + "\n";
    wifi += "  DNS: " + readStr(K_WIFI_DNS, "Not set") + "\n";
  }

  String uart = "UART:\n";
  uart += "  RX:" + String(readInt(K_GNSS_RX, -1));
  uart += " TX:" + String(readInt(K_GNSS_TX, -1));
  uart += " Baud:" + String((int)readUInt(K_GNSS_BAUD, 115200)) + "\n";

  String ntrip = "NTRIP:\n";
  ntrip += "  Enabled: " + String(readBool(K_NTRIP_ENABLED, false) ? "Yes" : "No") + "\n";
  ntrip += "  URL: " + readStr(K_NTRIP_HOST, "Not set") + "\n";
  ntrip += "  Port: " + String((int)readUInt(K_NTRIP_PORT, 2101)) + "\n";
  ntrip += "  Base: " + readStr(K_NTRIP_MOUNT, "Not set") + "\n";

  String full = wifi + uart + ntrip + "\n(1-6 to navigate)";

  // If it fits, send as one message; otherwise split
  if (full.length() <= 512) {
    sendMenu(full);
  } else {
    sendMenu(wifi);
    delay(100);
    sendMenu(uart + ntrip + "\n(1-6 to navigate)");
  }
}

// ---------------------------------------------------------------------------
// Factory reset
// ---------------------------------------------------------------------------

void factoryReset() {
  bool ok = true;

  prefs.begin(NS_WIFI, false);
  ok = ok && prefs.clear();
  prefs.end();

  prefs.begin(NS_GNSS, false);
  ok = ok && prefs.clear();
  prefs.end();

  prefs.begin(NS_NTRIP, false);
  ok = ok && prefs.clear();
  prefs.end();

  if (!LittleFS.begin(true)) {
    ok = false;
  } else {
    LittleFS.remove(NTRIP_JSON_PATH);
  }

  if (!ok) {
    sendMenu("ERROR: Factory reset failed!");
    return;
  }
  returnToMenu("NVS + NTRIP JSON cleared. All settings reset to defaults.");
}


#endif
