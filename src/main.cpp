// =============================================================================
// LilyGO T5 V2.4 Tarot – shuffle and read tarot cards on a 2.13" e-paper display
//
// Button 1 (GPIO39)  click : draw a new reading        | description: next page
//                    hold  : open / close description  | spread overview: open first card
// BOOT     (GPIO0)   click : splash -> show last reading | spread: cycle Past/Present/Future
//                            single card -> splash       | description: back to card
//                    hold  : toggle Single <-> 3-card spread mode
// =============================================================================
#include <Arduino.h>
#include "../include/t5_pins.h"
#include "../include/config.h"
#include "display.h"
#include "buttons.h"
#include "tarot.h"
#include "power.h"
#include "state_manager.h"

static AppState currentState = STATE_SPLASH;
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
// Rendering
// -----------------------------------------------------------------------------

static void showSplash() {
    Reading last;
    bool hasLast = loadLastReadingFromNvs(last);
    SplashInfo info = {currentMode, cardsRemaining(), hasLast ? &last : nullptr};
    drawSplashScreen(info);
    currentState = STATE_SPLASH;
}

static void showFocusedCard() {
    drawCardFace(currentReading.cards[focus], currentReading.isReversed(focus), positionLabel());
    currentState = STATE_CARD_FACE;
}

static void showSpread() {
    showFocusedCard();
}

static void showDescription() {
    pageCount = drawCardDescription(currentReading.cards[focus], currentReading.isReversed(focus),
                                    positionLabel(), page);
    currentState = STATE_CARD_DESC;
}

// Show a reading's entry screen
static void showReading() {
    focus = 0;
    page = 0;
    showFocusedCard();
}

static void doShuffleAnimation() {
    for (int i = 0; i < SHUFFLE_FRAMES; i++) {
        drawShuffleFrame(i, i == 0);
        delay(SHUFFLE_FRAME_MS);
        checkButtons();   // keep button state machine alive (and gather entropy)
    }
    consumeButtonEvent(); // drop presses made during the animation
}

static void newReading() {
    doShuffleAnimation();
    drawReading(currentReading, currentMode == MODE_SPREAD ? SPREAD_SIZE : 1);
    saveLastReadingToNvs(currentReading);
    showReading();
}

static void toggleMode() {
    currentMode = currentMode == MODE_SINGLE ? MODE_SPREAD : MODE_SINGLE;
    saveModeToNvs(currentMode);
    showSplash();
}

// -----------------------------------------------------------------------------
// Event handling per state
// -----------------------------------------------------------------------------

static void handleEvent(ButtonEvent event) {
    if (event == BOOT_LONG_PRESS) {
        toggleMode();
        return;
    }

    switch (currentState) {
        case STATE_SPLASH:
            if (event == BTN1_CLICK || event == BTN1_LONG_PRESS) {
                newReading();
            } else if (event == BOOT_CLICK) {
                // "Card of the day": bring back the last reading
                if (loadLastReadingFromNvs(currentReading)) showReading();
            }
            break;

        case STATE_CARD_FACE:
            if (event == BTN1_CLICK) {
                newReading();
            } else if (event == BTN1_LONG_PRESS) {
                page = 0;
                showDescription();
            } else if (event == BOOT_CLICK) {
                if (currentReading.count > 1) {
                    focus = (focus + 1) % currentReading.count;
                    showFocusedCard();
                } else {
                    showSplash();
                }
            }
            break;

        case STATE_SPREAD:
            if (event == BTN1_CLICK) {
                newReading();
            } else if (event == BTN1_LONG_PRESS || event == BOOT_CLICK) {
                focus = 0;
                showFocusedCard();
            }
            break;

        case STATE_CARD_DESC:
            if (event == BTN1_CLICK) {
                if (pageCount > 1) {
                    page = (page + 1) % pageCount;
                    showDescription();
                }
            } else if (event == BTN1_LONG_PRESS || event == BOOT_CLICK) {
                showFocusedCard();
            }
            break;
    }
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
        // E-ink still shows the last screen – restore state without redrawing
        currentState = snap.state;
        currentMode = snap.mode;
        currentReading = snap.reading;
        focus = snap.focus < SPREAD_SIZE ? snap.focus : 0;
        page = snap.page;
        if (currentReading.count == 0 && currentState != STATE_SPLASH) {
            showSplash();
        } else if (currentState == STATE_CARD_DESC) {
            pageCount = cardDescriptionPageCount(currentReading.cards[focus], currentReading.isReversed(focus));
        }
        Serial.println("[READY] Woke from deep sleep, state restored");
    } else {
        // Fresh boot (reset / power on)
        loadLastReadingFromNvs(currentReading);
        showSplash();
        Serial.println("[READY] T5 Tarot booted fresh");
    }

    lastActivityTime = millis();
}

void loop() {
    checkButtons();

    ButtonEvent event = consumeButtonEvent();
    if (event != BTN_NONE) {
        handleEvent(event);
        lastActivityTime = millis();   // after handling, so slow refreshes don't count as idle time
    }

    // Deep sleep after inactivity – the e-paper keeps the last screen visible
    if (millis() - lastActivityTime > INACTIVITY_TIMEOUT_MS) {
        UiSnapshot snap = {0, currentState, currentMode, focus, page, currentReading};
        saveSnapshotToRtc(snap);
        displayHibernate();
        enterDeepSleep();
    }

    delay(5);
}

