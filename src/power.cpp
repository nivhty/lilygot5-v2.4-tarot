#include "power.h"
#include "../include/t5_pins.h"
#include "../include/config.h"
#include <WiFi.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

void powerInit() {
    // GPIO12 was held LOW during deep sleep – release it before driving it again
    gpio_hold_dis((gpio_num_t)EPD_PWR_EN);
    gpio_deep_sleep_hold_dis();

    // Radios are never used
    WiFi.mode(WIFI_OFF);
    btStop();

    // Power on the e-paper (V2.4 only)
    pinMode(EPD_PWR_EN, OUTPUT);
    digitalWrite(EPD_PWR_EN, HIGH);

    analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
}

bool wokeFromButton() {
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
}

void enterDeepSleep() {
    // Cut display power. GPIO12 is a strapping pin (flash voltage select), so it must be LOW
    // at reset anyway; hold it LOW through deep sleep to keep the panel unpowered.
    digitalWrite(EPD_PWR_EN, LOW);
    gpio_hold_en((gpio_num_t)EPD_PWR_EN);
    gpio_deep_sleep_hold_en();

    // Wake on Button 1 (active LOW, external pull-up)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_1_PIN, 0);

    Serial.println("[SLEEP] Entering deep sleep");
    Serial.flush();
    esp_deep_sleep_start();
}

uint16_t readBatteryMv() {
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) {
        sum += analogReadMilliVolts(BATTERY_ADC_PIN);
    }
    return (uint16_t)((sum / 16) * 2);   // On-board divider ratio = 2
}

uint8_t batteryPercent() {
    uint16_t mv = readBatteryMv();
    if (mv >= BATTERY_FULL_MV) return 100;
    if (mv <= BATTERY_EMPTY_MV) return 0;
    return (uint8_t)((uint32_t)(mv - BATTERY_EMPTY_MV) * 100 / (BATTERY_FULL_MV - BATTERY_EMPTY_MV));
}

