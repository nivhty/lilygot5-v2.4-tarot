#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>

// Button event types for the main loop to consume
enum ButtonEvent : uint8_t {
    BTN_NONE = 0,
    BTN1_CLICK,         // Short press on Button 1 (GPIO39)
    BTN1_LONG_PRESS,    // Long press on Button 1
    BOOT_CLICK,         // Short press on BOOT (GPIO0)
    BOOT_LONG_PRESS,    // Long press on BOOT
};

void setupButtons();
void checkButtons();
ButtonEvent consumeButtonEvent();  // Returns latest event and clears it

#endif // BUTTONS_H

