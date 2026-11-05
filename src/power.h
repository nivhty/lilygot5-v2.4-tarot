#ifndef POWER_H
#define POWER_H

#include <Arduino.h>

void powerInit();               // Release sleep pin holds, radios off, display power on
bool wokeFromButton();          // True if this boot is a wake-up from deep sleep via Button 1
void enterDeepSleep();          // Display power off, wake on Button 1. Never returns.
uint16_t readBatteryMv();
uint8_t batteryPercent();

// T5 V2.4 has no charger-status pin, so plug/unplug is inferred from a jump in battery voltage.
// Call every ~1 s. Returns +1 when a charger was just plugged in, -1 when unplugged, 0 otherwise.
int8_t chargerEvent();
bool isCharging();

#endif // POWER_H

