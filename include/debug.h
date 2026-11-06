#ifndef DEBUG_H
#define DEBUG_H

#include <Arduino.h>

// Set to 0 to silence the button / refresh / sleep trace on the serial monitor
#ifndef DEBUG_TRACE
#define DEBUG_TRACE 1
#endif

#if DEBUG_TRACE
// Timestamped log line: "[   12345] message"
#define DBG(fmt, ...) Serial.printf("[%8lu] " fmt "\n", millis(), ##__VA_ARGS__)
#else
#define DBG(fmt, ...) do {} while (0)
#endif

#endif // DEBUG_H
