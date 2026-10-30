# Documentation for this esp32 board is at:
https://wiki.lilygo.cc/products/t5-series/t5-epaper-2.13inch/
Official repo (pins + examples): https://github.com/Xinyuan-LilyGO/LilyGo-T5-Epaper-Series

# Project description
Using a lilygo t5 to shuffle tarot card on the eink display, read the documentation for pin and how to control the board. Feels free to recommend feature.

# UI
Full vertical / portrait orientation (122 x 250 px):
1. **COVER MODE (On Reboot / Fresh Boot)**:
   - Displays ONLY the ornate back of a tarot card (120 x 240 px, edge-to-edge).
   - No battery, no splash text, no title. Clean, mystical tarot deck back.
2. **SHUFFLE ANIMATION (Holding BTN1)**:
   - Holding Button 1 (GPIO 39) shows a continuous, fluid shuffling animation.
   - Partial refresh with 8-bit byte aligned coordinates (`wx=0, ww=120`).
   - "Shuffling..." and "Release to draw" text.
3. **DRAW ON RELEASE (Hold Duration Entropy)**:
   - When Button 1 is released, the exact press duration (both ms and us) is calculated.
   - The hold duration is folded into the entropy pool and triggers dynamic deck cuts/swaps.
   - Draws a random card and displays Card Face with a full refresh.
4. **CARD DISPLAY (High Quality Art + Title Only)**:
   - Card image maximized to **120 x 220 px** (88% of screen height, edge-to-edge), rotated 180° when reversed (upside down).
   - Beneath the art: **CARD NAME (Up / Down status)**, e.g. `The Fool (Up)` or `The Tower (Down)`.
   - No descriptions on the card face; no battery indicator.
5. **DETAIL MODE (Tap BTN1 or BOOT)**:
   - Complete card description: Arcana info, Upright/Reversed meaning, Keywords, Opposite meaning.
   - Paginated if necessary.
   - Tapping BTN1 or BOOT advances the page or returns to Card Face.
   - Holding BTN1 starts shuffling for a new card.
   - Holding BOOT on Card Face returns to Cover Mode.

---

# PLAN (executed – see §10 for status)

## 1. Hardware facts (verified from official wiki + repo README/examples)

| Item | Value |
| --- | --- |
| SoC | ESP32 (dual-core LX6, 240 MHz), 4 MB flash, **no PSRAM** |
| Panel | 2.13" 122 x 250, SSD1680. Default **DEPG0213BN** (`GxEPD2_213_BN`); alt variant GDEM0213B74 (`GxEPD2_213_B74`) |
| Display SPI | CS 5, DC 17, RST 16, BUSY 4, SCLK 18, MOSI 23, MISO n/c |
| Display power enable | **GPIO 12** (V2.4 only) – drive HIGH before `display.init()` |
| Button 1 | GPIO 39 (input-only, active LOW, RTC-capable → deep-sleep wake) |
| Button BOOT | GPIO 0 (active LOW, strapping pin – usable after boot) |
| Battery ADC | GPIO 35, divider x2 (`analogReadMilliVolts(35) * 2`) |
| SD card | SCK 14, MISO 2, MOSI 15, CS 13 (separate bus; remove card while flashing) |
| Not present vs T-Echo | No backlight, no touch pad, no LoRa/GPS, no RGB LED |

Official init sequence (from examples):
```cpp
pinMode(12, OUTPUT); digitalWrite(12, HIGH);
SPI.begin(18, 19, 23);
display.init(115200, true, 2, false);
```

## 2. Project setup
- PlatformIO, `platform = espressif32@6.12.0`, `board = esp32dev`, `framework = arduino`,
  `board_build.partitions = huge_app.csv`, `monitor_speed = 115200`, `upload_speed = 921600`.
- Libraries: `zinggjm/GxEPD2`, `adafruit/Adafruit GFX Library`, `olikraus/U8g2_for_Adafruit_GFX`
  (nicer proportional fonts than FreeMono, matches official examples), `bxparks/AceButton`.
- Build flag to switch panel: `-DPANEL_DEPG0213BN` (default) or `-DPANEL_GDEM0213B74`.

## 3. Code reuse from `../t-echo-tarot`
| T-Echo file | Action for T5 |
| --- | --- |
| `include/tarot_deck.h`, `src/tarot_deck.cpp` | Copy as-is (78 cards, meanings, keywords) |
| `tools/convert_images.py` | Copy, add `--size WxH` option and T5 header comment; regenerate bitmaps |
| `cards/*.jpg` | Reference via path (`../t-echo-tarot/cards`) – no need to duplicate 78 JPGs |
| `include/config.h` | Rewrite for 250x122, new timings |
| `include/t_echo_pins.h` | Replace with `include/t5_pins.h` (table above) |
| `src/display.*` | Port GxEPD (old) → **GxEPD2** paged API (`firstPage/nextPage`, `setPartialWindow`) |
| `src/buttons.*` | Keep AceButton; Button1 = main, BOOT = secondary; drop touch |
| `src/power.*` | Rewrite: ESP32 deep sleep, `esp_sleep_enable_ext0_wakeup(GPIO_NUM_39, 0)`, EPD power off (GPIO12 LOW), WiFi/BT off |
| `src/state_manager.*` | `RTC_DATA_ATTR` for sleep-wake state + `Preferences` (NVS) for persistence across resets |
| `src/tarot.*` | Use `esp_random()` (hardware RNG) instead of ADC-seeded `random()` |
| `src/main.cpp` | Same state machine (SPLASH → CARD_FACE ↔ CARD_DESC), adapted to new inputs |

## 4. UI layout (landscape 250 x 122, buttons side TBD on hardware)

Source card art is 804x1363 (≈ 0.59 aspect, vertical). Scale card to full screen height:

**Card bitmap: 72 x 122 px** (width multiple of 8 → 1098 B/card, ~86 KB for 78 cards; fits flash easily).

```
+--------+--------------------------------------+  250 x 122
|        |  THE TOWER  (R)          [bat 87%]   |
|  card  |  ----------------------------------  |
|  72x122|  Upheaval, Sudden change,            |
| (rot.  |  Revelation                          |
|  180°  |                                      |
| if rev)|  #16 · Major Arcana                  |
+--------+--------------------------------------+
  x=0..71   x=78..246 text area (~168 px wide)
```
- Card face: card on the left at full height, name (bold, wraps to 2 lines) + keywords + suit/number on the right. Text is horizontal (no rotated text needed like on T-Echo, since the 2:1 screen leaves room beside the card).
- Reversed cards: bitmap drawn **rotated 180°** (T-Echo never actually flipped it) + "(R)" in name.
- Description screen: full 250x122 width, header "Upright/Reversed – Card name", wrapped meaning + keywords, paginated (page height ~110 px) with small page indicator "1/2".
- Splash: title "Tarot" + small card-back pattern + hint "Press to draw".
- Shuffle animation: partial refresh of a small window ("Shuffling..." + stacked card outlines sliding), ~5 frames, then full refresh for the card (avoids ghosting).
- Full refresh every N partial updates (e.g. 5) to keep the panel clean.

## 5. Controls
| Input | Cover Mode (Reboot) | Card Face | Detail Mode |
| --- | --- | --- | --- |
| **Button 1 Hold** | **Shuffling animation** (continuous while held) → **Release draws card** (duration entropy) | **Shuffling animation** → **Release draws card** (duration entropy) | **Shuffling animation** → **Release draws card** |
| **Button 1 Tap** (< 350ms) | Shuffles & draws card | **Open Detail Mode** | Next page / Back to Card Face |
| **BOOT Tap** | Show last reading or shuffle | **Open Detail Mode** | Back to Card Face |
| **BOOT Hold** (>= 500ms) | – | **Return to Cover Mode** | Back to Card Face |

## 6. Power
- WiFi + BT off at boot (`WiFi.mode(WIFI_OFF); btStop();`).
- After `INACTIVITY_TIMEOUT_MS` (60 s): `display.hibernate()`, GPIO12 LOW, store state in RTC RAM, ext0 wake on GPIO39 LOW, `esp_deep_sleep_start()`. E-ink keeps the last card as a "screensaver".
- On wake from ext0: restore state, **don't redraw**, treat the waking press as consumed (same 1 s guard as T-Echo).
- Battery % from GPIO35 shown as small icon on card face (4.2 V full / 3.3 V empty).

## 7. Recommended features (pick which to include)
1. **Three-card spread** (Past / Present / Future): three 72x122 cards fill 216 px + labels — fits the 2:1 screen perfectly. Long-press BOOT to switch Single ↔ Spread mode.
2. **Card of the day**: persist last draw + date-less "draw count" in NVS so the same card survives a reset/battery swap.
3. **No-repeat shuffle**: Fisher-Yates deck in NVS, draw without replacement until the deck is exhausted.
4. **Draw history**: last 10 cards stored in NVS, browsable with BOOT.
5. **Shake-free "cut the deck"**: hold Button1 while the shuffle animates; release time mixes into the RNG.
6. Low-battery warning screen before sleep when < 10 %.

## 8. Execution steps
1. Scaffold PlatformIO project (`platformio.ini`, `include/`, `src/`, `tools/`, `.gitignore`), `git init`.
2. Copy deck data + converter; extend converter with `--size 72x122`; generate `include/tarot_bitmaps.h`.
3. Hello-world display test (GPIO12, GxEPD2_213_BN, rotation=1 landscape) – confirm orientation/button side.
4. Port display module (splash, card face, description, shuffle) to GxEPD2 + U8g2 fonts.
5. Port buttons, tarot RNG, state machine.
6. Power/deep-sleep + RTC/NVS state.
7. Selected features from §7.
8. `pio run` compile check (and flash/test on hardware by user).

## 9. Open questions
- Which features from §7 to include? (Recommended: 1, 2, 3)
- Which panel variant do you have (DEPG0213BN default, or GDEM0213B74)? The board sticker/FPC label tells.
- Landscape orientation: buttons on the left or right side when holding it? (Will default to rotation 1, adjustable via `BASE_ROTATION`.)


## 10. Implementation Status & Hardware Verification

- **Codebase Built**: Project fully set up with PlatformIO (`espressif32@6.12.0`, `esp32dev`).
- **Features Implemented**:
  1. **Single Card & 3-Card Spread (Past / Present / Future)**:
     - In single mode: 72x122 vertical card on the left + upright/reversed indicator, card name, keywords, and Roman numeral/arcana info on the right.
     - In 3-card spread mode: 3 cards drawn side-by-side with Past / Present / Future labels at the top or bottom depending on card orientation.
     - Long-press BOOT to toggle between Single and Spread modes.
  2. **Card of the Day / Persistent State**: Last reading saved to NVS and shown on the splash screen. Press BOOT on splash to instantly view the saved reading.
  3. **No-Repeat Fisher-Yates Shuffle**: Tracks deck state in NVS so all 78 cards are drawn before an automatic reshuffle.
  4. **Dynamic 180° Inversion**: Reversed cards are drawn rotated 180° (true reversed orientation).
  5. **Deep Sleep & Battery**: Hibernates display, cuts GPIO12 power rail, stores UI state in RTC memory, and configures GPIO39 (Button 1) as EXT0 wakeup source.
- **Compilation Output**:
  - RAM: 15.8% (51.9 KB / 320 KB)
  - Flash: 32.3% (1.0 MB / 3.1 MB)
  - Target: `.pio/build/lilygo_t5_v24/firmware.bin`

### Flashing to Hardware
Connect the board via USB-C and run:
```bash
cd /home/tanvan/Work/lilygot5-v2.4-tarot
pio run -t upload
pio device monitor -b 115200
```
*(Note: If a MicroSD card is inserted, remove it during upload due to strapping pin GPIO 2)*


## 10. Implementation Status & Hardware Verification

- **Codebase Built**: Project fully set up with PlatformIO (`espressif32@6.12.0`, `esp32dev`).
- **Features Implemented**:
  1. **Single Card & 3-Card Spread (Past / Present / Future)**:
     - In single mode: 72x122 vertical card on the left + upright/reversed indicator, card name, keywords, and Roman numeral/arcana info on the right.
     - In 3-card spread mode: 3 cards drawn side-by-side with Past / Present / Future labels at the top or bottom depending on card orientation.
     - Long-press BOOT to toggle between Single and Spread modes.
  2. **Card of the Day / Persistent State**: Last reading saved to NVS and shown on the splash screen. Press BOOT on splash to instantly view the saved reading.
  3. **No-Repeat Fisher-Yates Shuffle**: Tracks deck state in NVS so all 78 cards are drawn before an automatic reshuffle.
  4. **Dynamic 180° Inversion**: Reversed cards are drawn rotated 180° (true reversed orientation).
  5. **Deep Sleep & Battery**: Hibernates display, cuts GPIO12 power rail, stores UI state in RTC memory, and configures GPIO39 (Button 1) as EXT0 wakeup source.
- **Compilation Output**:
  - RAM: 15.8% (51.9 KB / 320 KB)
  - Flash: 32.3% (1.0 MB / 3.1 MB)
  - Target: `.pio/build/lilygo_t5_v24/firmware.bin`

### Flashing to Hardware
Connect the board via USB-C and run:
```bash
cd /home/tanvan/Work/lilygot5-v2.4-tarot
pio run -t upload
pio device monitor -b 115200
```
*(Note: If a MicroSD card is inserted, remove it during upload due to strapping pin GPIO 2)*
