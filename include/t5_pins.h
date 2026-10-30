#ifndef T5_PINS_H
#define T5_PINS_H

// LilyGO T5 V2.4 pin map
// Source: https://github.com/Xinyuan-LilyGO/LilyGo-T5-Epaper-Series (README "Pins Map")

// --- E-paper (SPI) ---
#define EPD_PWR_EN      12   // Display power control (V2.4 only). HIGH = on. Strapping pin: keep LOW at reset.
#define EPD_MOSI        23
#define EPD_MISO        19   // Not connected on the board; passed to SPI.begin like the official examples
#define EPD_SCLK        18
#define EPD_CS          5
#define EPD_DC          17
#define EPD_RST         16
#define EPD_BUSY        4

// --- Buttons (both active LOW) ---
#define BUTTON_1_PIN    39   // Input-only, external pull-up, RTC GPIO -> deep sleep wake source
#define BUTTON_BOOT_PIN 0    // BOOT button (strapping pin, usable as input after boot)

// --- Battery ---
#define BATTERY_ADC_PIN 35   // Voltage divider x2

// --- SD card (separate bus, unused) ---
#define SD_SCK          14
#define SD_MISO         2
#define SD_MOSI         15
#define SD_CS           13

#endif // T5_PINS_H

