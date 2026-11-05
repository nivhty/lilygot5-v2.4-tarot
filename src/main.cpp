// =============================================================================
// LilyGO T5 V2.4 Tarot – shuffle and read tarot cards on a 2.13" e-paper display
//
// Reboot / Power On: Boots into Cover Mode (only shows the back of a tarot card)
// Button 1 (GPIO 39) Hold   : Continuous shuffling animation; release to draw card
//                             (draw entropy includes exact held duration in ms/us)
//                    Click  : Open Detail Mode (on Card Face) / Next page (in Detail Mode)
// BOOT     (GPIO 0)  Click  : Open Detail Mode (on Card Face) / Back to Card Face (in Detail)
//                    Hold   : Return to Cover Mode
// =============================================================================
#include <Arduino.h>
#include "../include/t5_pins.h"
#include "../include/config.h"
#include "display.h"
#include "buttons.h"
#include "tarot.h"
#include "power.h"
#include "state_manager.h"

static AppState currentState = STATE_COVER;
static DrawMode currentMode = MODE_SINGLE;
static Reading  currentReading = {0, {0, 0, 0}, 0};
static uint8_t  focus = 0;         // Focused card index within the reading
static uint8_t  page = 0;          // Description page
static uint8_t  pageCount = 1;
static unsigned long lastActivityTime = 0;

static const char* const POSITION_LABELS[SPREAD_SIZE] = {"Past", "Present", "Future"};

static const char* positionLabel() {
    return currentReading.count > 1 ? POSITION_LABELS[focus] : nullptr;
}

// -----------------------------------------------------------------------------
// Rendering helpers
// -----------------------------------------------------------------------------

static void showCover() {
    drawCoverScreen();
    currentState = STATE_COVER;
}

static void showFocusedCard(bool partial = false) {
    drawCardFace(currentReading.cards[focus], currentReading.isReversed(focus), positionLabel(), partial);
    currentState = STATE_CARD_FACE;
}

static void showDescription(bool partial = true) {
    pageCount = drawCardDescription(currentReading.cards[focus], currentReading.isReversed(focus),
                                    positionLabel(), page, partial);
    currentState = STATE_CARD_DESC;
}

static void redrawCurrent() {
    if (currentState == STATE_CARD_FACE) showFocusedCard();
    else if (currentState == STATE_CARD_DESC) showDescription(false);
    else showCover();
}

static void pollCharger() {
    static unsigned long lastPoll = 0;
    if (millis() - lastPoll < 1000) return;
    lastPoll = millis();
    if (chargerEvent() > 0) {
        drawChargingScreen(batteryPercent());
        delay(CHARGING_SCREEN_MS);
        redrawCurrent();
        lastActivityTime = millis();
    }
}

static void doShuffleAndDraw(unsigned long pressStartMs, unsigned long pressStartUs) {
    int frame = 0;
    // Frame 0: first frame full window partial refresh
    drawShuffleFrame(frame++, true);

    // Keep animating shuffle frames as long as Button 1 is held down
    while (digitalRead(BUTTON_1_PIN) == LOW) {
        drawShuffleFrame(frame++, false);
        tarotAddEntropy((uint32_t)micros());
        delay(10);
    }

    // Ensure at least 2 frames are shown for visual feedback even on quick release
    while (frame < 2) {
        drawShuffleFrame(frame++, false);
        delay(30);
    }

    unsigned long pressEndMs = millis();
    unsigned long pressEndUs = micros();
    unsigned long heldDurationMs = pressEndMs - pressStartMs;
    unsigned long heldDurationUs = pressEndUs - pressStartUs;

    Serial.printf("[SHUFFLE] Released! Held for %lu ms (%lu us), frames: %d\n",
                  heldDurationMs, heldDurationUs, frame);

    // Dynamic deck shuffle based on the calculated hold time
    tarotShuffleWithDuration((uint32_t)heldDurationMs, (uint32_t)heldDurationUs);

    // Draw reading and persist
    drawReading(currentReading, currentMode == MODE_SPREAD ? SPREAD_SIZE : 1);
    saveLastReadingToNvs(currentReading);

    // Show the card face
    focus = 0;
    page = 0;
    showFocusedCard();
}

// -----------------------------------------------------------------------------
// Arduino entry points
// -----------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);

    powerInit();
    setupDisplay();
    setupButtons();
    tarotInit();

    currentMode = loadModeFromNvs();

    UiSnapshot snap;
    if (wokeFromButton() && loadSnapshotFromRtc(snap)) {
        // Restoring from deep sleep
        currentState = snap.state;
        currentMode = snap.mode;
        currentReading = snap.reading;
        focus = snap.focus < SPREAD_SIZE ? snap.focus : 0;
        page = snap.page;
        if (currentReading.count == 0 && currentState != STATE_COVER) {
            showCover();
        } else if (currentState == STATE_CARD_DESC) {
            pageCount = cardDescriptionPageCount(currentReading.cards[focus], currentReading.isReversed(focus));
        }
        Serial.println("[READY] Woke from deep sleep, state restored");
    } else {
        // Fresh boot (reset / power on / flash): ALWAYS boot into Cover Mode!
        loadLastReadingFromNvs(currentReading);
        showCover();
        currentState = STATE_COVER;
        Serial.println("[READY] T5 Tarot booted into Cover Mode");
    }

    chargerEvent();   // establish voltage baseline (no popup at boot)
    lastActivityTime = millis();
}

void loop() {
    pollCharger();

    // -------------------------------------------------------------------------
    // 1. Button 1 (GPIO 39) - Active LOW
    // -------------------------------------------------------------------------
    if (digitalRead(BUTTON_1_PIN) == LOW) {
        delay(25); // Debounce
        if (digitalRead(BUTTON_1_PIN) == LOW) {
            unsigned long pressStartMs = millis();
            unsigned long pressStartUs = micros();

            if (currentState == STATE_COVER) {
                // In Cover mode: immediately start shuffling while held!
                doShuffleAndDraw(pressStartMs, pressStartUs);
                lastActivityTime = millis();
            } else if (currentState == STATE_CARD_FACE) {
                // In Card Face mode: wait up to 350ms to distinguish Click (details) vs Hold (shuffle)
                while (digitalRead(BUTTON_1_PIN) == LOW && (millis() - pressStartMs < 350)) {
                    delay(10);
                }
                if (digitalRead(BUTTON_1_PIN) == LOW) {
                    // Still held after 350ms -> user is holding to shuffle a new card!
                    doShuffleAndDraw(pressStartMs, pressStartUs);
                } else {
                    // Released before 350ms -> click to open Detail Mode!
                    page = 0;
                    showDescription();
                }
                lastActivityTime = millis();
            } else if (currentState == STATE_CARD_DESC) {
                // In Detail mode:
                while (digitalRead(BUTTON_1_PIN) == LOW && (millis() - pressStartMs < 350)) {
                    delay(10);
                }
                if (digitalRead(BUTTON_1_PIN) == LOW) {
                    // Still held after 350ms -> start shuffling new card!
                    doShuffleAndDraw(pressStartMs, pressStartUs);
                } else {
                    // Click -> advance page or return to card face
                    if (pageCount > 1 && page + 1 < pageCount) {
                        page++;
                        showDescription();
                    } else {
                        showFocusedCard(true);
                    }
                }
                lastActivityTime = millis();
            }
        }
    }

    // -------------------------------------------------------------------------
    // 2. BOOT Button (GPIO 0) - Active LOW
    // -------------------------------------------------------------------------
    if (digitalRead(BUTTON_BOOT_PIN) == LOW) {
        delay(25); // Debounce
        if (digitalRead(BUTTON_BOOT_PIN) == LOW) {
            unsigned long bootStartMs = millis();
            while (digitalRead(BUTTON_BOOT_PIN) == LOW && (millis() - bootStartMs < 500)) {
                delay(10);
            }
            bool bootHeld = (digitalRead(BUTTON_BOOT_PIN) == LOW);
            // Wait for release
            while (digitalRead(BUTTON_BOOT_PIN) == LOW) {
                delay(10);
            }

            if (currentState == STATE_COVER) {
                if (currentReading.count > 0) {
                    showFocusedCard();
                } else {
                    doShuffleAndDraw(bootStartMs, micros());
                }
            } else if (currentState == STATE_CARD_FACE) {
                if (bootHeld) {
                    showCover();
                } else {
                    page = 0;
                    showDescription();
                }
            } else if (currentState == STATE_CARD_DESC) {
                showFocusedCard(true);
            }
            lastActivityTime = millis();
        }
    }

    // -------------------------------------------------------------------------
    // 3. Deep Sleep on Inactivity (e-paper keeps screen image with zero power)
    // -------------------------------------------------------------------------
    if (millis() - lastActivityTime > INACTIVITY_TIMEOUT_MS) {
        UiSnapshot snap = {0, currentState, currentMode, focus, page, currentReading};
        saveSnapshotToRtc(snap);
        displayHibernate();
        enterDeepSleep();
    }

    delay(10);
}
