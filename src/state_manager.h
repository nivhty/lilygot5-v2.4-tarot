#ifndef STATE_MANAGER_H
#define STATE_MANAGER_H

#include <Arduino.h>
#include "../include/config.h"

enum AppState : uint8_t {
    STATE_SPLASH = 0,
    STATE_CARD_FACE,    // Single card (or one focused card of a spread)
    STATE_SPREAD,       // Three-card overview
    STATE_CARD_DESC,    // Text description of the focused card
};

enum DrawMode : uint8_t {
    MODE_SINGLE = 0,
    MODE_SPREAD = 1,
};

// One reading = 1 card (single mode) or 3 cards (Past / Present / Future)
struct Reading {
    uint8_t count;                  // 0 = no reading yet
    uint8_t cards[SPREAD_SIZE];
    uint8_t reversedMask;           // bit i = card i reversed

    bool isReversed(uint8_t i) const { return (reversedMask >> i) & 1; }
};

// Snapshot of the UI kept in RTC memory across deep sleep (lost on reset / power cycle)
struct UiSnapshot {
    uint32_t magic;
    AppState state;
    DrawMode mode;
    uint8_t  focus;
    uint8_t  page;
    Reading  reading;
};

// --- RTC (deep sleep) ---
void saveSnapshotToRtc(const UiSnapshot& snap);
bool loadSnapshotFromRtc(UiSnapshot& snap);   // false if no valid snapshot

// --- NVS (survives reset / battery swap) ---
void     saveModeToNvs(DrawMode mode);
DrawMode loadModeFromNvs();
void     saveLastReadingToNvs(const Reading& r);   // "Card of the day"
bool     loadLastReadingFromNvs(Reading& r);

#endif // STATE_MANAGER_H

