#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include "state_manager.h"

struct SplashInfo {
    DrawMode       mode;
    uint8_t        cardsLeft;
    const Reading* lastReading;   // nullptr if none saved
};

void setupDisplay();
void displayHibernate();

void drawSplashScreen(const SplashInfo& info);

// Single vertical card face:
// Top: CARD IMAGE (upside down if reversed)
// Mid: CARD NAME (Up/ Down status)
// Btm: SHORT DESCRIPTION
void drawCardFace(uint8_t cardIdx, bool reversed, const char* positionLabel = nullptr);

// Three-card overview or focused card in spread
void drawSpread(const Reading& reading);

// Full detailed description page (when holding button)
uint8_t drawCardDescription(uint8_t cardIdx, bool reversed, const char* positionLabel, uint8_t page);
uint8_t cardDescriptionPageCount(uint8_t cardIdx, bool reversed);

// Single frame of shuffle animation (partial refresh)
void drawShuffleFrame(int frame, bool firstFrame);

#endif // DISPLAY_H
