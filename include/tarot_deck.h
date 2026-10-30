#ifndef TAROT_DECK_H
#define TAROT_DECK_H

#include <Arduino.h>

enum TarotSuit : uint8_t {
    SUIT_MAJOR = 0,
    SUIT_WANDS,
    SUIT_CUPS,
    SUIT_SWORDS,
    SUIT_PENTACLES
};

struct TarotCard {
    const char* name;       // e.g. "The Fool", "Ace of Wands"
    TarotSuit   suit;
    uint8_t     number;     // 0-21 for Major, 1-14 for Minor
    const char* upright;    // Upright meaning (2-3 sentences, must fit on 200px wide e-ink with word wrap)
    const char* reversed;   // Reversed meaning (2-3 sentences)
    const char* keywords;   // 2-4 comma-separated keywords
};

static const uint8_t DECK_SIZE = 78;

extern const TarotCard tarotDeck[DECK_SIZE] PROGMEM;

#endif // TAROT_DECK_H
