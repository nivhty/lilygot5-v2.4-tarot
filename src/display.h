#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include "state_manager.h"

struct SplashInfo {
    DrawMode       mode;
    uint8_t        cardsLeft;
    const Reading* lastReading;   // nullptr if none saved
    uint8_t        batteryPct;
};

void setupDisplay();
void displayHibernate();

void drawSplashScreen(const SplashInfo& info);

// Single card face. positionLabel is "Past"/"Present"/"Future" in spread mode, nullptr otherwise.
void drawCardFace(uint8_t cardIdx, bool reversed, const char* positionLabel, uint8_t batteryPct);

// Three cards side by side
void drawSpread(const Reading& reading);

// Description page. Returns the total number of pages.
uint8_t drawCardDescription(uint8_t cardIdx, bool reversed, const char* positionLabel, uint8_t page);
uint8_t cardDescriptionPageCount(uint8_t cardIdx, bool reversed);

// One frame of the shuffle animation (partial refresh)
void drawShuffleFrame(int frame, bool firstFrame);

#endif // DISPLAY_H
