# lilygot5-v2.4-tarot

Tarot card reader and shuffler designed for the **LilyGO T5 V2.4** ESP32 development board with a 2.13-inch monochrome e-paper display (SSD1680 / DEPG0213BN, 122 × 250 px).

---

## ✨ Features

- **Cover Mode on Boot**: Displays only the intricate, ornate back of a tarot card (120 × 240 px) with crisp full refresh. Zero text or status clutter.
- **Hold-to-Shuffle Animation**: Holding **Button 1** triggers a continuous, animated shuffling sequence on the e-paper display.
- **Release-to-Draw with Timing Entropy**: Measures the exact millisecond and microsecond duration Button 1 was held, folding it into the hardware RNG entropy pool and executing dynamic deck cuts and swaps.
- **Edge-to-Edge High Quality Art**: Card artwork auto-cropped and maximized to 120 × 220 px (88% of vertical screen height). Upside down when reversed.
- **Card Display**: Shows the card illustration and title with `(Up)` or `(Down)` position.
- **Detail Mode**: Comprehensive card breakdown including Arcana classification, upright/reversed meanings, keywords, and opposite counterpart.
- **Power Efficiency**: Automatic deep sleep after 60 seconds of inactivity; e-paper keeps the display image without consuming battery power. Wakes instantly via Button 1.

---

## 🕹️ Controls

| Action | Screen | Result |
| --- | --- | --- |
| **Hold Button 1 (GPIO 39)** | Any | Continuous shuffling animation; **release** to draw a card |
| **Tap Button 1 (< 350ms)** | Cover Mode | Shuffles and draws a card |
| **Tap Button 1 (< 350ms)** | Card Face | Opens **Detail Mode** |
| **Tap Button 1 (< 350ms)** | Detail Mode | Next page / returns to Card Face |
| **Tap BOOT (GPIO 0)** | Card Face | Opens **Detail Mode** |
| **Tap BOOT (GPIO 0)** | Detail Mode | Returns to Card Face |
| **Hold BOOT (>= 500ms)** | Card Face | Returns to **Cover Mode** |

---

## 🔌 Hardware Pinout (LilyGO T5 V2.4)

| Signal | ESP32 GPIO | Description |
| --- | --- | --- |
| **EPD_PWR_EN** | GPIO 12 | Display power control (active HIGH) |
| **EPD_CS** | GPIO 5 | SPI Chip Select |
| **EPD_DC** | GPIO 17 | Data / Command |
| **EPD_RST** | GPIO 16 | Reset |
| **EPD_BUSY** | GPIO 4 | Busy pin |
| **EPD_SCLK** | GPIO 18 | SPI Clock |
| **EPD_MOSI** | GPIO 23 | SPI MOSI |
| **Button 1** | GPIO 39 | Main user button (RTC wake source, active LOW) |
| **BOOT** | GPIO 0 | Secondary button (active LOW) |
| **Battery ADC**| GPIO 35 | Voltage divider (x2) |

---

## 🚀 Building & Flashing

This project uses [PlatformIO](https://platformio.org/).

### Compile:
```bash
pio run
```

### Upload via USB:
```bash
pio run -t upload --upload-port /dev/ttyACM0
```

### Serial Monitor:
```bash
pio device monitor -b 115200
```

