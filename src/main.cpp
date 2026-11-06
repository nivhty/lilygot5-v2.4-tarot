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
#include "../include/debug.h"

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

static void redrawCurrent(bool partial) {
    if (currentState == STATE_CARD_FACE) showFocusedCard(partial);
    else if (currentState == STATE_CARD_DESC) showDescription(partial);
    else drawCoverScreen(partial);
}

// Charging popup is non-blocking: it stays up for CHARGING_SCREEN_MS or until any button is pressed
static bool chargingShown = false;
static unsigned long chargingShownAt = 0;

static void showChargingPopup() {
    drawChargingScreen(batteryPercent(), true);
    chargingShown = true;
    chargingShownAt = millis();
    lastActivityTime = millis();
}

static void pollCharger() {
    static unsigned long lastPoll = 0;
    if (millis() - lastPoll < 1000) return;
    lastPoll = millis();
    if (chargerEvent() > 0) {
        DBG("[PWR] Charger detected (%u mV)", readBatteryMv());
        showChargingPopup();
    }
}

// Set only for the tap that woke the device (from light or deep sleep) when it was already released
// before the code could see it. Taps made while the display is refreshing are deliberately NOT replayed:
// a repeated tap during a slow refresh would undo the first one (open description, then close it again).
static bool btn1Pending = false;

// A button only counts once it has been seen released after the last screen update.
// This drops presses that started during a refresh and are still held when it finishes.
static bool btn1Armed = true;
static bool bootArmed = true;

// Called every ~1 ms while the panel is busy. Polls the pin instead of using an interrupt:
// GPIO39 is prone to spurious edges whenever the ADC is read (battery level), which caused
// endless phantom clicks. A press must stay LOW for ~15 ms to count, and is latched once.
static void pollButton1WhileBusy() {
    static uint8_t lowMs = 0;
    if (digitalRead(BUTTON_1_PIN) == LOW) {
        if (lowMs < 255) lowMs++;
        if (lowMs == 15) btn1Pending = true;   // LOW for 15 ms = a real press
    } else {
        lowMs = 0;
    }
}

static const char* stateName(AppState s) {
    switch (s) {
        case STATE_COVER: return "COVER";
        case STATE_CARD_FACE: return "CARD_FACE";
        case STATE_CARD_DESC: return "CARD_DESC";
        default: return "OTHER";
    }
}

// Click action (short press) on the card face / description pages
static void handleButton1Click() {
    DBG("[BTN1] Click in %s (page %u/%u)", stateName(currentState), page + 1, pageCount);
    if (currentState == STATE_CARD_FACE) {
        page = 0;
        showDescription();
    } else if (currentState == STATE_CARD_DESC) {
        if (pageCount > 1 && page + 1 < pageCount) {
            page++;
            showDescription();
        } else {
            showFocusedCard(true);
        }
    }
    // Any tap made during this click's screen refresh must NOT replay and undo the click!
    btn1Pending = false;
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
    tarotCommit(currentReading);

    // Show the card face (taps made while holding/shuffling must not replay as clicks)
    delay(30);
    btn1Pending = false;
    focus = 0;
    page = 0;
    showFocusedCard();
}

// Idle handling: light sleep first (instant wake, partial refresh still possible), deep sleep later.
// Returns when a button is pressed or a charger is plugged in; loop() then handles it.
static void idleSleep() {
    DBG("[SLEEP] Idle %lu ms -> light sleep (state %s)", millis() - lastActivityTime, stateName(currentState));
    displayPowerOff();
    while (true) {
        const unsigned long idle = millis() - lastActivityTime;
        if (idle >= DEEP_SLEEP_AFTER_MS) {
            UiSnapshot snap = {0, currentState, currentMode, focus, page, currentReading};
            saveSnapshotToRtc(snap);
            displayHibernate();
            DBG("[SLEEP] Idle %lu ms -> deep sleep", idle);
            enterDeepSleep();   // never returns
        }
        uint32_t ms = DEEP_SLEEP_AFTER_MS - idle;
        if (ms > IDLE_CHARGER_POLL_MS) ms = IDLE_CHARGER_POLL_MS;

        if (lightSleep(ms)) {
            // Button wake takes ~1 ms, so the button is normally still down and loop() sees it.
            // If it was already released (very quick tap), replay it as a Button 1 click.
            if (digitalRead(BUTTON_1_PIN) == HIGH && digitalRead(BUTTON_BOOT_PIN) == HIGH) {
                btn1Pending = true;
            }
            btn1Armed = bootArmed = true;     // this press began while asleep: it is a real press
            lastActivityTime = millis();      // stay awake even if the press is not recognised
            DBG("[SLEEP] Woke from light sleep by button: BTN1=%s BOOT=%s%s",
                digitalRead(BUTTON_1_PIN) == LOW ? "down" : "up",
                digitalRead(BUTTON_BOOT_PIN) == LOW ? "down" : "up",
                btn1Pending ? " -> already released, will replay as click" : "");
            return;
        }
        if (chargerEvent() > 0) {
            DBG("[PWR] Charger detected during light sleep (%u mV)", readBatteryMv());
            showChargingPopup();
            return;
        }
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
    displaySetBusyHook(pollButton1WhileBusy);
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
        // The press that woke us is usually released by the time we get here (~0.3-0.5 s boot):
        // act on it as a click instead of making the user press again.
        if (digitalRead(BUTTON_1_PIN) == HIGH) btn1Pending = true;
        DBG("[READY] Woke from deep sleep in %lu ms, state %s, BTN1 %s", millis(), stateName(currentState),
            btn1Pending ? "already released -> will replay as click" : "still down");
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
    // Loop stall detector: a gap > 150 ms between iterations means buttons were not being polled
    static unsigned long lastLoopMs = millis();
    const unsigned long gap = millis() - lastLoopMs;
    // (Gaps caused by a handled button action or sleep are expected: those update lastActivityTime.)
    if (gap > 150 && millis() - lastActivityTime > gap) {
        DBG("[LOOP] Buttons were not polled for %lu ms (unexpected blocking)", gap);
    }

    if (chargingShown) {
        const bool pressed = digitalRead(BUTTON_1_PIN) == LOW || digitalRead(BUTTON_BOOT_PIN) == LOW || btn1Pending;
        if (pressed || millis() - chargingShownAt >= CHARGING_SCREEN_MS) {
            // A press only dismisses the popup; wait for release so it is not also taken as a click
            while (digitalRead(BUTTON_1_PIN) == LOW || digitalRead(BUTTON_BOOT_PIN) == LOW) delay(10);
            chargingShown = false;
            redrawCurrent(true);
            btn1Pending = false;
            btn1Armed = bootArmed = false;
            lastActivityTime = millis();
        }
        delay(10);
        lastLoopMs = millis();
        return;
    }

    pollCharger();

    // The tap that woke the device was already released: act on it now
    if (btn1Pending && digitalRead(BUTTON_1_PIN) == HIGH) {
        btn1Pending = false;
        if (currentState != STATE_COVER) {
            Serial.println("[BTN1] Wake tap -> click");
            handleButton1Click();
            btn1Armed = false;
            lastActivityTime = millis();
        }
    }

    // Re-arm buttons once they are seen released
    static bool loggedNotArmed = false;
    if (digitalRead(BUTTON_1_PIN) == HIGH) {
        btn1Armed = true;
        loggedNotArmed = false;
    } else if (!btn1Armed && !loggedNotArmed) {
        DBG("[BTN1] Press IGNORED: button was already down when the last screen update finished");
        loggedNotArmed = true;
    }
    if (digitalRead(BUTTON_BOOT_PIN) == HIGH) bootArmed = true;

    // -------------------------------------------------------------------------
    // 1. Button 1 (GPIO 39) - Active LOW
    // -------------------------------------------------------------------------
    if (btn1Armed && digitalRead(BUTTON_1_PIN) == LOW) {
        const unsigned long seenMs = millis();
        delay(25); // Debounce
        if (digitalRead(BUTTON_1_PIN) != LOW) {
            DBG("[BTN1] Press IGNORED: released within 25 ms debounce (glitch or very short tap)");
        } else {
            unsigned long pressStartMs = millis();
            unsigned long pressStartUs = micros();
            btn1Pending = false;   // this press is handled right here
            DBG("[BTN1] Press detected in %s", stateName(currentState));

            if (currentState == STATE_COVER) {
                // In Cover mode: immediately start shuffling while held!
                doShuffleAndDraw(pressStartMs, pressStartUs);
                lastActivityTime = millis();
                btn1Armed = false;
            } else if (currentState == STATE_CARD_FACE) {
                // In Card Face mode: wait up to 350ms to distinguish Click (details) vs Hold (shuffle)
                while (digitalRead(BUTTON_1_PIN) == LOW && (millis() - pressStartMs < 350)) {
                    delay(10);
                }
                if (digitalRead(BUTTON_1_PIN) == LOW) {
                    DBG("[BTN1] Held > 350 ms -> shuffle");
                    // Still held after 350ms -> user is holding to shuffle a new card!
                    doShuffleAndDraw(pressStartMs, pressStartUs);
                } else {
                    DBG("[BTN1] Released after %lu ms -> click", millis() - seenMs);
                    // Released before 350ms -> click to open Detail Mode!
                    delay(30); btn1Pending = false;   // drop release bounce before the blocking refresh
                    handleButton1Click();
                }
                lastActivityTime = millis();
                btn1Armed = false;
            } else if (currentState == STATE_CARD_DESC) {
                // In Detail mode:
                while (digitalRead(BUTTON_1_PIN) == LOW && (millis() - pressStartMs < 350)) {
                    delay(10);
                }
                if (digitalRead(BUTTON_1_PIN) == LOW) {
                    DBG("[BTN1] Held > 350 ms -> shuffle");
                    // Still held after 350ms -> start shuffling new card!
                    doShuffleAndDraw(pressStartMs, pressStartUs);
                } else {
                    DBG("[BTN1] Released after %lu ms -> click", millis() - seenMs);
                    // Click -> advance page or return to card face
                    delay(30); btn1Pending = false;
                    handleButton1Click();
                }
                lastActivityTime = millis();
                btn1Armed = false;
            }
        }
    }

    // -------------------------------------------------------------------------
    // 2. BOOT Button (GPIO 0) - Active LOW
    // -------------------------------------------------------------------------
    if (bootArmed && digitalRead(BUTTON_BOOT_PIN) == LOW) {
        delay(25); // Debounce
        if (digitalRead(BUTTON_BOOT_PIN) == LOW) {
            unsigned long bootStartMs = millis();
            while (digitalRead(BUTTON_BOOT_PIN) == LOW && (millis() - bootStartMs < LONG_PRESS_MS)) {
                delay(10);
            }
            const bool bootHeld = (digitalRead(BUTTON_BOOT_PIN) == LOW);
            DBG("[BOOT] %s in %s", bootHeld ? "Hold" : "Click", stateName(currentState));

            // Act immediately (hold acts at the threshold, no need to let go first)
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

            // Wait for release so a long hold does not repeat the action
            while (digitalRead(BUTTON_BOOT_PIN) == LOW) {
                delay(10);
            }
            lastActivityTime = millis();
            bootArmed = false;
            btn1Armed = false;
        }
    }

    // -------------------------------------------------------------------------
    // 3. Idle: light sleep after INACTIVITY_TIMEOUT_MS, deep sleep after DEEP_SLEEP_AFTER_MS
    // -------------------------------------------------------------------------
    if (millis() - lastActivityTime > INACTIVITY_TIMEOUT_MS && !btn1Pending) {
        idleSleep();
        lastLoopMs = millis();
        return;
    }

    delay(10);
    lastLoopMs = millis();
}
