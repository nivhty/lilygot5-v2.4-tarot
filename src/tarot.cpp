#include "tarot.h"
#include <Preferences.h>
#include <esp_random.h>
#include <bootloader_random.h>

// Persistent no-repeat deck:
//   deck[0 .. pos-1]  = already drawn
//   deck[pos .. 77]   = remaining (order irrelevant, picked uniformly at draw time)
static uint8_t deck[DECK_SIZE];
static uint8_t deckPos = 0;
static uint32_t entropyPool = 0;

static const char* NVS_NAMESPACE = "tarot";

// splitmix32-style finalizer
static uint32_t mix32(uint32_t z) {
    z += 0x9E3779B9;
    z = (z ^ (z >> 16)) * 0x85EBCA6B;
    z = (z ^ (z >> 13)) * 0xC2B2AE35;
    return z ^ (z >> 16);
}

void tarotAddEntropy(uint32_t value) {
    entropyPool = mix32(entropyPool ^ value);
}

static uint32_t nextRandom() {
    // esp_random() is only a true RNG while RF or the bootloader entropy source is on,
    // so also fold in the pool (seeded with true entropy at boot) and the current time.
    entropyPool = mix32(entropyPool ^ esp_random() ^ (uint32_t)micros());
    return entropyPool;
}

// Unbiased random number in [0, n)
static uint32_t randomBelow(uint32_t n) {
    uint32_t limit = UINT32_MAX - (UINT32_MAX % n);
    uint32_t r;
    do { r = nextRandom(); } while (r >= limit);
    return r % n;
}

void tarotShuffleWithDuration(uint32_t holdMs, uint32_t holdUs) {
    // Fold hold duration and microsecond resolution into entropy pool
    tarotAddEntropy(holdMs);
    tarotAddEntropy(holdUs);
    tarotAddEntropy((holdMs * 2654435761U) ^ holdUs);

    // Extra dynamic deck cuts / swaps directly based on the calculated hold time:
    // Even a 1ms or 1us difference will alter the swap indices and card ordering!
    uint32_t cuts = (holdMs % 30) + 7;
    for (uint32_t c = 0; c < cuts; c++) {
        uint32_t i = randomBelow(DECK_SIZE);
        uint32_t j = randomBelow(DECK_SIZE);
        uint8_t tmp = deck[i];
        deck[i] = deck[j];
        deck[j] = tmp;
    }
}

static void resetDeck() {
    for (uint8_t i = 0; i < DECK_SIZE; i++) deck[i] = i;
    deckPos = 0;
}

static bool deckIsValid() {
    if (deckPos > DECK_SIZE) return false;
    bool seen[DECK_SIZE] = {false};
    for (uint8_t i = 0; i < DECK_SIZE; i++) {
        if (deck[i] >= DECK_SIZE || seen[deck[i]]) return false;
        seen[deck[i]] = true;
    }
    return true;
}

static void saveDeck() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBytes("deck", deck, DECK_SIZE);
    prefs.putUChar("pos", deckPos);
    prefs.end();
}

static void loadDeck() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    size_t n = prefs.getBytesLength("deck") == DECK_SIZE ? prefs.getBytes("deck", deck, DECK_SIZE) : 0;
    deckPos = prefs.getUChar("pos", 0);
    prefs.end();
    if (n != DECK_SIZE || !deckIsValid()) resetDeck();
}

void tarotInit() {
    // Gather true random seed from the SAR ADC noise source, then release it
    // (bootloader_random conflicts with normal ADC use, e.g. battery reading).
    bootloader_random_enable();
    for (int i = 0; i < 16; i++) {
        tarotAddEntropy(esp_random());
        delayMicroseconds(50);
    }
    bootloader_random_disable();
    tarotAddEntropy((uint32_t)esp_timer_get_time());

    loadDeck();
}

void drawReading(Reading& out, uint8_t count) {
    if (count == 0) count = 1;
    if (count > SPREAD_SIZE) count = SPREAD_SIZE;

    out.count = count;
    out.reversedMask = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (deckPos >= DECK_SIZE) resetDeck();   // Deck exhausted -> reshuffle
        uint8_t j = deckPos + randomBelow(DECK_SIZE - deckPos);
        uint8_t tmp = deck[deckPos];
        deck[deckPos] = deck[j];
        deck[j] = tmp;
        out.cards[i] = deck[deckPos++];
        if (randomBelow(2)) out.reversedMask |= (1 << i);
    }
    saveDeck();
}

uint8_t cardsRemaining() {
    return DECK_SIZE - deckPos;
}

const TarotCard& getCard(uint8_t index) {
    if (index >= DECK_SIZE) index = 0;
    return tarotDeck[index];
}

