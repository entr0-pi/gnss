#ifndef LOGGER_H
#define LOGGER_H

/*
 * Logger System - Use Case Documentation
 * ---------------------------------------
 * At the top of each .cpp file, define MODULE_LOG to enable/disable logging for that file
 * before including logger.h in each translation unit :
 * #define MODULE_LOG 1
 * #define MODULE_LOG 0  // disable logs for this file
 * 
 * 1. System Boot (Info Level)
 *    Global: GLOBAL_LOG_LEVEL 3
 *    Code:   LOG_I("SYS", "Initialisation v%s", APP_VERSION);
 *    Output:  [SYS] Initialisation v2.1.0
 *
 * 2. Deep Debugging (Debug Level)
 *    Global: GLOBAL_LOG_LEVEL 4
 *    Code:   LOG_D("MEM", "Stack High Watermark: %u", uxTaskGetStackHighWaterMark(NULL));
 *    Output:  [1.045][MEM] Stack High Watermark: 4096 (main.cpp:45)
 *
 * 3. Critical Errors
 *    Global: GLOBAL_LOG_LEVEL 1
 *    Code:   LOG_E("I2C", "SDA/SCL Line short circuit detected");
 *    Output:  [I2C] SDA/SCL Line short circuit detected (Printed in Red)
 *
 * 4. Muting a Specific Module
 *    Global: GLOBAL_LOG_LEVEL 4
 *    Code:
 *      #define MODULE_LOG 0 // Mute this file
 *      #include "Logger.h"
 *      LOG_I("GPS", "Searching for satellites...");
 *    Output:  (Silence - No strings stored in Flash for this call)
 *
 * 5. Hardware Warnings
 *    Global: GLOBAL_LOG_LEVEL 2
 *    Code:   LOG_W("TEMP", "Overheating: %d C", currentTemp);
 *    Output:  [TEMP] Overheating: 85 C (Printed in Yellow)
 *
 * 6. Battery Monitoring
 *    Global: GLOBAL_LOG_LEVEL 3
 *    Code:   LOG_I("BAT", "Charging: %d%%", soc);
 *    Output:  [BAT] Charging: 92%
 *
 * 7. State Machine Tracking
 *    Global: GLOBAL_LOG_LEVEL 3
 *    Code:   LOG_I("FSM", "State Change: IDLE -> BUSY");
 *    Output:  [FSM] State Change: IDLE -> BUSY
 *
 * 8. Wi-Fi Credentials Handling
 *    Global: GLOBAL_LOG_LEVEL 4
 *    Code:   LOG_D("WIFI", "Connecting to %s", ssid);
 *    Output:  [0.450][WIFI] Connecting to MyWiFi (WiFiMgr.cpp:12)
 *
 * 9. Production Build (Stripping Debug)
 *    Global: GLOBAL_LOG_LEVEL 3
 *    Code:   LOG_D("PERF", "Task execution took %d ms", duration);
 *    Output:  (Nothing - String is stripped from binary to save space)
 *
 * 10. Memory Tracking (Heap)
 *     Global: GLOBAL_LOG_LEVEL 4
 *     Code:   LOG_D("RAM", "Free: %d", ESP.getFreeHeap());
 *     Output:  [5.200][RAM] Free: 195000 (main.cpp:110)
 */

#include <Arduino.h>
#include "app.h"

#if LOG_USE_COLOR
  #define CLR_ERR  "\033[0;31m"
  #define CLR_WRN  "\033[0;33m"
  #define CLR_INF  "\033[0;32m"
  #define CLR_DBG  "\033[0;36m"
  #define CLR_RST  "\033[0m"
#else
  #define CLR_ERR  ""
  #define CLR_WRN  ""
  #define CLR_INF  ""
  #define CLR_DBG  ""
  #define CLR_RST  ""
#endif

#ifndef MODULE_LOG
#error "Define MODULE_LOG (0 or 1) before including logger.h in each translation unit."
#endif

void logToSerial(const char* color, const char* title, const char* file, int line, const char* fmt, ...);

#define LOG_DISPATCH(color, title, fmt, ...) \
    logToSerial(color, title, (GLOBAL_LOG_LEVEL >= 4 ? __FILE__ : NULL), (GLOBAL_LOG_LEVEL >= 4 ? __LINE__ : 0), fmt, ##__VA_ARGS__)

#if GLOBAL_LOG_LEVEL >= 1
  #define LOG_E(title, fmt, ...) if(MODULE_LOG) LOG_DISPATCH(CLR_ERR, title, fmt, ##__VA_ARGS__)
#else
  #define LOG_E(title, ...)
#endif

#if GLOBAL_LOG_LEVEL >= 2
  #define LOG_W(title, fmt, ...) if(MODULE_LOG) LOG_DISPATCH(CLR_WRN, title, fmt, ##__VA_ARGS__)
#else
  #define LOG_W(title, ...)
#endif

#if GLOBAL_LOG_LEVEL >= 3
  #define LOG_I(title, fmt, ...) if(MODULE_LOG) LOG_DISPATCH(CLR_INF, title, fmt, ##__VA_ARGS__)
#else
  #define LOG_I(title, ...)
#endif

#if GLOBAL_LOG_LEVEL >= 4
  #define LOG_D(title, fmt, ...) if(MODULE_LOG) LOG_DISPATCH(CLR_DBG, title, fmt, ##__VA_ARGS__)
#else
  #define LOG_D(title, ...)
#endif

#endif
