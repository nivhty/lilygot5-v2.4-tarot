#ifndef TAROT_H
#define TAROT_H

#include <Arduino.h>
#include "../include/tarot_deck.h"
#include "state_manager.h"

// Seed the entropy pool (hardware RNG) and load the persisted deck from NVS
void tarotInit();

// Mix extra entropy (e.g. button timing) into the pool
void tarotAddEntropy(uint32_t value);

// Draw `count` cards without replacement from the persistent deck.
// The deck is reshuffled automatically once all 78 cards have been drawn.
void drawReading(Reading& out, uint8_t count);

// Cards left before the deck is reshuffled
uint8_t cardsRemaining();

// Get card data by index
const TarotCard& getCard(uint8_t index);

#endif // TAROT_H

