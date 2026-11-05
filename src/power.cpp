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

    // Drop light-sleep wake sources (timer/GPIO), then wake on Button 1 only (active LOW, external pull-up)
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_1_PIN, 0);

    Serial.println("[SLEEP] Entering deep sleep");
    Serial.flush();
    esp_deep_sleep_start();
}

bool lightSleep(uint32_t ms) {
    gpio_wakeup_enable((gpio_num_t)BUTTON_1_PIN, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)BUTTON_BOOT_PIN, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);

    Serial.flush();
    esp_light_sleep_start();

    const bool byButton = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO;
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    gpio_wakeup_disable((gpio_num_t)BUTTON_1_PIN);
    gpio_wakeup_disable((gpio_num_t)BUTTON_BOOT_PIN);
    return byButton;
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


// Charger detection: while charging, the battery node rises by ~0.1 V or more versus idle.
static bool charging = false;
static uint16_t baselineMv = 0;

bool isCharging() { return charging; }

int8_t chargerEvent() {
    const uint16_t mv = readBatteryMv();
    if (baselineMv == 0) {                       // first sample after boot
        baselineMv = mv;
        charging = mv >= BATTERY_CHARGING_MV;
        return charging ? 1 : 0;
    }
    int8_t ev = 0;
    if (!charging && (mv >= baselineMv + CHARGE_JUMP_MV || mv >= BATTERY_CHARGING_MV)) {
        charging = true; ev = 1;
    } else if (charging && mv + CHARGE_JUMP_MV <= baselineMv && mv < BATTERY_CHARGING_MV) {
        charging = false; ev = -1;
    }
    if (ev != 0) {
        baselineMv = mv;
    } else {
        baselineMv = (uint16_t)((baselineMv * 7 + mv) / 8);   // slow tracking of drift
    }
    return ev;
}
