#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include "state_manager.h"

void setupDisplay();
void displayHibernate();

// Cover mode: displays only the ornate back of a tarot card (full refresh)
void drawCoverScreen();

// Single vertical card face:
// Top: CARD IMAGE (upside down if reversed, 120 x 220)
// Mid: CARD NAME (Up/ Down status)
void drawCardFace(uint8_t cardIdx, bool reversed, const char* positionLabel = nullptr);

// Three-card overview or focused card in spread
void drawSpread(const Reading& reading);

// Full detailed description page (meanings, keywords, arcana)
uint8_t drawCardDescription(uint8_t cardIdx, bool reversed, const char* positionLabel, uint8_t page);
uint8_t cardDescriptionPageCount(uint8_t cardIdx, bool reversed);

// Single frame of shuffle animation (partial refresh)
void drawShuffleFrame(int frame, bool firstFrame);

#endif // DISPLAY_H
