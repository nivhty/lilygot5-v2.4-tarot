#ifndef POWER_H
#define POWER_H

#include <Arduino.h>

void powerInit();               // Release sleep pin holds, radios off, display power on
bool wokeFromButton();          // True if this boot is a wake-up from deep sleep via Button 1
void enterDeepSleep();          // Display power off, wake on Button 1. Never returns.
uint16_t readBatteryMv();
uint8_t batteryPercent();

#endif // POWER_H

