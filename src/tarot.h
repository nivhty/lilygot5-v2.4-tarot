#ifndef TAROT_H
#define TAROT_H

#include <Arduino.h>
#include "../include/tarot_deck.h"
#include "state_manager.h"

// Seed the entropy pool (hardware RNG) and load the persisted deck from NVS
void tarotInit();

// Mix extra entropy (e.g. button timing) into the pool
void tarotAddEntropy(uint32_t value);

// Mix button hold duration (ms and us) into the entropy pool and shuffle the deck
void tarotShuffleWithDuration(uint32_t holdMs, uint32_t holdUs);

// Draw `count` cards without replacement from the persistent deck.
// The deck is reshuffled automatically once all 78 cards have been drawn.
void drawReading(Reading& out, uint8_t count);

// Persist the deck state and the last reading in a single NVS write session.
// Call right after drawReading().
void tarotCommit(const Reading& last);

// Cards left before the deck is reshuffled
uint8_t cardsRemaining();

// Get card data by index
const TarotCard& getCard(uint8_t index);

#endif // TAROT_H

