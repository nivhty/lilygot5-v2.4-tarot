#include "buttons.h"
#include "../include/t5_pins.h"

void setupButtons() {
    pinMode(BUTTON_1_PIN, INPUT);           // GPIO39 has external pull-up on LilyGO T5
    pinMode(BUTTON_BOOT_PIN, INPUT_PULLUP); // GPIO0 internal pull-up
}
