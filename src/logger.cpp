#define MODULE_LOG 1
#include "logger.h"
#include <stdarg.h>

void logToSerial(const char* color, const char* title, const char* file, int line, const char* fmt, ...) {
    Serial.print(color);
    if (GLOBAL_LOG_LEVEL >= 4) {
        uint32_t now = millis();
        Serial.printf("[%lu.%03lu]", now / 1000, now % 1000);
    }
    Serial.printf("[%s] ", title);
    char buffer[256]; 
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Serial.print(buffer);
    if (file != NULL) {
        const char* fileName = strrchr(file, '/') ? strrchr(file, '/') + 1 : 
                               (strrchr(file, '\\') ? strrchr(file, '\\') + 1 : file);
        Serial.printf(" (%s:%d)", fileName, line);
    }
    Serial.println(CLR_RST);
}
