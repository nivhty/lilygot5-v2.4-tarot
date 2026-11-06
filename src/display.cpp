#include "display.h"
#include <SPI.h>
#include <vector>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include "../include/t5_pins.h"
#include "../include/config.h"
#include "../include/tarot_deck.h"
#include "../include/tarot_bitmaps.h"
#include "tarot.h"
#include "power.h"
#include "../include/debug.h"

#define PAD 4   // Screen padding (px) on all four sides

// -----------------------------------------------------------------------------
// Panel selection (see platformio.ini build_flags)
// -----------------------------------------------------------------------------
#if defined(PANEL_GDEM0213B74)
typedef GxEPD2_213_B74 PanelDriver;
#else
typedef GxEPD2_213_BN PanelDriver;   // DEPG0213BN (default on T5 V2.4)
#endif

// Full frame buffer (128x250 / 8 = 4 KB)
static GxEPD2_BW<PanelDriver, PanelDriver::HEIGHT> display(
    PanelDriver(/*CS*/ EPD_CS, /*DC*/ EPD_DC, /*RST*/ EPD_RST, /*BUSY*/ EPD_BUSY));
static U8G2_FOR_ADAFRUIT_GFX u8g2;

static uint8_t partialCount = 0;

// Fonts
#define FONT_TITLE  u8g2_font_helvB14_tf
#define FONT_NAME   u8g2_font_helvB10_tf
#define FONT_BOLD   u8g2_font_helvB08_tf
#define FONT_BODY   u8g2_font_helvR08_tf
#define FONT_SMALL  u8g2_font_5x8_tf

static const char* const SPREAD_LABELS[SPREAD_SIZE] = {"Past", "Present", "Future"};
static const char* const SUIT_NAMES[] = {"Major Arcana", "Wands", "Cups", "Swords", "Pentacles"};
static const char* const ROMAN[] = {
    "0", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X",
    "XI", "XII", "XIII", "XIV", "XV", "XVI", "XVII", "XVIII", "XIX", "XX", "XXI"};

static String arcanaInfo(const TarotCard& card) {
    if (card.suit == SUIT_MAJOR) {
        return String(card.number <= 21 ? ROMAN[card.number] : "?") + " - Major Arcana";
    }
    return String("Minor Arcana - ") + SUIT_NAMES[card.suit <= SUIT_PENTACLES ? card.suit : 0];
}

// -----------------------------------------------------------------------------
// Low-level helpers
// -----------------------------------------------------------------------------

static bool partialPrimed = false;

static void pushScreen(bool partial) {
    const unsigned long t0 = millis();
    const bool doPartial = partial && partialCount < FULL_REFRESH_EVERY;
    DBG("[EPD] %s refresh start%s", doPartial ? "partial" : "full",
        (partial && !doPartial) ? " (forced: anti-ghosting)" : "");
    if (doPartial) {
        if (!partialPrimed) {
            // On DEPG0213BN (SSD1680), GxEPD2_213_BN::_Update_Part() calls _Init_Part() (writing
            // lut_partial to 0x32) BEFORE _PowerOn() (0x22 0xf8, which reloads the OTP LUT into 0x32),
            // causing the first partial refresh after any powerOff() to fail to move pixels while
            // writeImageAgain() still overwrites 0x26. Prime partial mode first with a 0-diff refresh
            // (0x24 == 0x26 from the previous frame) so _PowerOn() is already done before writeImage().
            display.epd2.refresh(true);
            partialPrimed = true;
        }
        display.display(true);
        partialCount++;
    } else {
        // If a partial refresh ran previously (e.g. shuffle animation), power off first so
        // _Update_Full() runs _PowerOn() (0x22 0xf8) and reloads the full-refresh OTP LUT.
        display.powerOff();
        display.display(false);
        // Prime partial mode immediately while 0x24 == 0x26 so the first button click is instant.
        display.epd2.refresh(true);
        partialPrimed = true;
        partialCount = 0;
    }
    DBG("[EPD] refresh done in %lu ms", millis() - t0);
}

static void beginFrame() {
    display.setFullWindow();
    display.fillScreen(GxEPD_WHITE);
}

static int16_t textWidth(const char* s) {
    return u8g2.getUTF8Width(s);
}

static void drawText(int16_t x, int16_t baseline, const char* s) {
    u8g2.setCursor(x, baseline);
    u8g2.print(s);
}

static void drawTextCentered(int16_t centerX, int16_t baseline, const char* s) {
    drawText(centerX - textWidth(s) / 2, baseline, s);
}

// Word-wrap `text` with current font into lines no wider than maxWidth
static void wrapText(const char* text, int16_t maxWidth, std::vector<String>& out, size_t maxLines = 255) {
    if (!text) return;
    String line, word;
    const size_t startCount = out.size();
    auto flushLine = [&]() {
        if (line.length() > 0) out.push_back(line);
        line = "";
    };

    for (const char* p = text;; ++p) {
        const char c = *p;
        if (c == ' ' || c == '\n' || c == '\0') {
            if (word.length() > 0) {
                String candidate = line.length() ? line + " " + word : word;
                if (line.length() && textWidth(candidate.c_str()) > maxWidth) {
                    flushLine();
                    line = word;
                } else {
                    line = candidate;
                }
                word = "";
            }
            if (c == '\n') flushLine();
            if (c == '\0') break;
        } else {
            word += c;
        }
    }
    flushLine();

    // Truncate with ellipsis if too many lines
    if (out.size() - startCount > maxLines) {
        out.resize(startCount + maxLines);
        String& last = out.back();
        while (last.length() > 0 && textWidth((last + "...").c_str()) > maxWidth) {
            last.remove(last.length() - 1);
        }
        last += "...";
    }
}

// Draw a 1-bit card bitmap, rotated 180° if reversed (upside down)
static void drawCardBitmap(int16_t x0, int16_t y0, uint8_t cardIdx, bool reversed) {
    const uint8_t* bmp = cardIdx < TAROT_TOTAL_CARDS ? tarotBitmaps[cardIdx] : nullptr;
    const int16_t w = TAROT_BMP_WIDTH, h = TAROT_BMP_HEIGHT;

    if (!bmp) {
        display.drawRect(x0, y0, w, h, GxEPD_BLACK);
        u8g2.setFont(FONT_BODY);
        drawTextCentered(x0 + w / 2, y0 + h / 2, "NO IMG");
        return;
    }

    // Frame is already white: skip all-white bytes and only plot black pixels
    const int16_t bytesPerRow = (w + 7) / 8;
    for (int16_t y = 0; y < h; y++) {
        const uint8_t* row = bmp + y * bytesPerRow;
        const int16_t py = reversed ? (y0 + h - 1 - y) : (y0 + y);
        for (int16_t bx = 0; bx < bytesPerRow; bx++) {
            const uint8_t b = pgm_read_byte(row + bx);
            if (b == 0) continue;
            for (uint8_t bit = 0; bit < 8; bit++) {
                if (!(b & (0x80 >> bit))) continue;
                const int16_t x = (bx << 3) + bit;
                if (x >= w) break;
                display.drawPixel(reversed ? (x0 + w - 1 - x) : (x0 + x), py, GxEPD_BLACK);
            }
        }
    }
}

// Procedural card back: ornate border, diamond lattice pattern, and celestial moon medallion
static void drawCardBack(int16_t x, int16_t y, int16_t w, int16_t h) {
    display.fillRect(x, y, w, h, GxEPD_WHITE);
    // Outer rounded borders
    display.drawRoundRect(x, y, w, h, 4, GxEPD_BLACK);
    display.drawRoundRect(x + 2, y + 2, w - 4, h - 4, 3, GxEPD_BLACK);

    if (w >= 80) {
        // Detailed ornate border for full-sized cover
        display.drawRect(x + 5, y + 5, w - 10, h - 10, GxEPD_BLACK);
        display.drawRect(x + 7, y + 7, w - 14, h - 14, GxEPD_BLACK);

        // Corner ornaments
        const int16_t cOff = 10;
        display.drawCircle(x + cOff, y + cOff, 2, GxEPD_BLACK);
        display.drawCircle(x + w - 1 - cOff, y + cOff, 2, GxEPD_BLACK);
        display.drawCircle(x + cOff, y + h - 1 - cOff, 2, GxEPD_BLACK);
        display.drawCircle(x + w - 1 - cOff, y + h - 1 - cOff, 2, GxEPD_BLACK);
    }

    // Diamond pattern
    const int16_t pad = (w >= 80) ? 10 : 4;
    const int16_t ix = x + pad, iy = y + pad, iw = w - 2 * pad, ih = h - 2 * pad;
    const int16_t step = (w >= 80) ? 12 : 8;
    for (int16_t d = -ih; d < iw; d += step) {
        for (int16_t t = 0; t < ih; t++) {
            int16_t px1 = ix + d + t;
            int16_t px2 = ix + iw - 1 - (d + t);
            if (px1 >= ix && px1 < ix + iw && (t & 1) == 0) display.drawPixel(px1, iy + t, GxEPD_BLACK);
            if (px2 >= ix && px2 < ix + iw && (t & 1) == 0) display.drawPixel(px2, iy + t, GxEPD_BLACK);
        }
    }

    // Center medallion
    const int16_t cx = x + w / 2, cy = y + h / 2;
    const int16_t r = (w >= 80) ? 26 : (w / 4);

    display.fillCircle(cx, cy, r + 4, GxEPD_WHITE);
    display.drawCircle(cx, cy, r + 4, GxEPD_BLACK);
    display.drawCircle(cx, cy, r + 2, GxEPD_BLACK);
    display.drawCircle(cx, cy, r, GxEPD_BLACK);
    display.fillCircle(cx, cy, r - 2, GxEPD_BLACK);

    // Crescent moon
    const int16_t mr = (r - 2) * 7 / 10;
    const int16_t mOffX = (r - 2) / 2;
    const int16_t mOffY = (r - 2) / 3;
    display.fillCircle(cx + mOffX, cy - mOffY, mr, GxEPD_WHITE);

    if (w >= 80) {
        // Celestial accents in cover mode: top and bottom 4-point stars
        const int16_t starL = 5;
        // Top star
        display.drawFastVLine(cx, y + 16, starL * 2 + 1, GxEPD_BLACK);
        display.drawFastHLine(cx - starL, y + 16 + starL, starL * 2 + 1, GxEPD_BLACK);
        // Bottom star
        display.drawFastVLine(cx, y + h - 16 - starL * 2 - 1, starL * 2 + 1, GxEPD_BLACK);
        display.drawFastHLine(cx - starL, y + h - 16 - starL - 1, starL * 2 + 1, GxEPD_BLACK);
    }
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

static void (*busyHook)() = nullptr;
static void busyCallback(const void*) {
    delay(1);
    if (busyHook) busyHook();
}

void displaySetBusyHook(void (*hook)()) {
    busyHook = hook;
    display.epd2.setBusyCallback(busyCallback);
}

void setupDisplay() {
    SPI.begin(EPD_SCLK, EPD_MISO, EPD_MOSI);
    display.init(0, true, 2, false);
    display.setRotation(BASE_ROTATION);
    display.setTextWrap(false);

    u8g2.begin(display);
    u8g2.setFontMode(1);
    u8g2.setFontDirection(0);
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setBackgroundColor(GxEPD_WHITE);
    partialCount = 0;
}

void displayHibernate() {
    partialPrimed = false;
    display.hibernate();
}

void displayPowerOff() {
    partialPrimed = false;
    display.powerOff();   // booster off, controller RAM kept -> partial refresh still works after light sleep
}

// Cover mode: displays ONLY the back of a tarot card (full refresh)
void drawCoverScreen(bool partial) {
    beginFrame();
    const int16_t bw = 120;
    const int16_t bh = 240;
    const int16_t bx = (SCREEN_WIDTH - bw) / 2; // 1 px
    const int16_t by = (SCREEN_HEIGHT - bh) / 2; // 5 px
    drawCardBack(bx, by, bw, bh);
    pushScreen(partial);   // full refresh by default: crisp & pristine
}

// Vertical Card Face
// 1. CARD IMAGE at top (120 x 220, upside down if reversed)
// 2. CARD NAME only (single line)
// 3. Orientation "(Upright)" / "(Reversed)" (with spread position if any) at the bottom
// All descriptions are in detail mode (accessed by holding the button)
void drawCardFace(uint8_t cardIdx, bool reversed, const char* positionLabel, bool partial) {
    const TarotCard& card = getCard(cardIdx);
    beginFrame();

    const int16_t cardX = (SCREEN_WIDTH - CARD_IMG_WIDTH) / 2;
    const int16_t cardY = 0;
    drawCardBitmap(cardX, cardY, cardIdx, reversed);

    const int16_t divY = cardY + CARD_IMG_HEIGHT + 1; // 221 px
    display.drawFastHLine(0, divY, SCREEN_WIDTH, GxEPD_BLACK);

    const int16_t maxW = SCREEN_WIDTH - 2 * PAD;

    // Name only. Longest deck name ("Knight of Pentacles") fits in bold; shrink font as a safeguard.
    u8g2.setFont(FONT_BOLD);
    if (textWidth(card.name) > maxW) u8g2.setFont(FONT_BODY);
    drawTextCentered(SCREEN_WIDTH / 2, divY + 13, card.name);

    // Orientation line at the very bottom
    String orient = reversed ? "(Reversed)" : "(Upright)";
    if (positionLabel) orient = String(positionLabel) + " " + orient;
    u8g2.setFont(FONT_BODY);
    drawTextCentered(SCREEN_WIDTH / 2, SCREEN_HEIGHT - PAD, orient.c_str());

    pushScreen(partial);
}

// Brief "Charging" screen with a battery icon and the current level
void drawChargingScreen(uint8_t percent, bool partial) {
    beginFrame();
    const int16_t bw = 60, bh = 30;
    const int16_t bx = (SCREEN_WIDTH - bw) / 2 - 3, by = 80;

    display.drawRect(bx, by, bw, bh, GxEPD_BLACK);
    display.drawRect(bx + 1, by + 1, bw - 2, bh - 2, GxEPD_BLACK);
    display.fillRect(bx + bw, by + 9, 5, bh - 18, GxEPD_BLACK);   // terminal nub
    const int16_t fillW = (int16_t)((bw - 8) * percent / 100);
    display.fillRect(bx + 4, by + 4, fillW, bh - 8, GxEPD_BLACK);

    // Lightning bolt (white where the fill is black, black elsewhere)
    const int16_t cx = bx + bw / 2, cy = by + bh / 2;
    display.fillTriangle(cx + 4, cy - 11, cx - 6, cy + 2, cx + 1, cy + 2, GxEPD_WHITE);
    display.fillTriangle(cx - 4, cy + 11, cx + 6, cy - 2, cx - 1, cy - 2, GxEPD_WHITE);
    display.drawLine(cx + 4, cy - 11, cx - 6, cy + 2, GxEPD_BLACK);
    display.drawLine(cx - 4, cy + 11, cx + 6, cy - 2, GxEPD_BLACK);

    u8g2.setFont(FONT_TITLE);
    drawTextCentered(SCREEN_WIDTH / 2, by + bh + 36, "Charging");
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", percent);
    u8g2.setFont(FONT_NAME);
    drawTextCentered(SCREEN_WIDTH / 2, by + bh + 58, buf);

    pushScreen(partial);
}

void drawSpread(const Reading& reading) {
    // In spread mode, focused card is shown full screen with its label
    if (reading.count > 0) {
        drawCardFace(reading.cards[0], reading.isReversed(0), SPREAD_LABELS[0]);
    }
}

// -----------------------------------------------------------------------------
// Extended Description (when holding button)
// -----------------------------------------------------------------------------

struct DescLine {
    String text;
    bool bold;
};

static const int16_t DESC_LINE_H = 12;
static const int16_t DESC_HEADER_BASELINE = PAD + 9;        // 4 px top padding + font ascent
static const int16_t DESC_HEADER_DIV_Y = DESC_HEADER_BASELINE + 4;
static const int16_t DESC_FIRST_BASELINE = DESC_HEADER_DIV_Y + 13;
static const int16_t DESC_FOOTER_DIV_Y = SCREEN_HEIGHT - 16;
static const int16_t DESC_FOOTER_BASELINE = SCREEN_HEIGHT - PAD - 1;
static const uint8_t DESC_LINES_PER_PAGE = 17;   // body area between header and footer dividers

// Wrap `text` in the bold or body font to the padded width and append it.
// Bold lines must be wrapped too: e.g. "Minor Arcana - Pentacles" is 126 px in bold, wider than the screen.
static void addWrapped(std::vector<DescLine>& out, const char* text, bool bold) {
    std::vector<String> tmp;
    u8g2.setFont(bold ? FONT_BOLD : FONT_BODY);
    wrapText(text, SCREEN_WIDTH - 2 * PAD, tmp);
    for (auto& s : tmp) out.push_back({s, bold});
}

static void buildDescription(const TarotCard& card, bool reversed, std::vector<DescLine>& out) {
    addWrapped(out, arcanaInfo(card).c_str(), true);

    addWrapped(out, reversed ? "Meaning (Reversed)" : "Meaning (Upright)", true);
    addWrapped(out, reversed ? card.reversed : card.upright, false);

    addWrapped(out, "Keywords", true);
    addWrapped(out, card.keywords, false);

    addWrapped(out, reversed ? "Opposite (Upright)" : "Opposite (Reversed)", true);
    addWrapped(out, reversed ? card.upright : card.reversed, false);
}

// Word-wrapped layout of the last viewed card, reused across page turns
static std::vector<DescLine> cachedDesc;
static int16_t cachedDescKey = -1;

static const std::vector<DescLine>& getDescription(uint8_t cardIdx, bool reversed) {
    const int16_t key = (int16_t)cardIdx * 2 + (reversed ? 1 : 0);
    if (key != cachedDescKey) {
        cachedDesc.clear();
        buildDescription(getCard(cardIdx), reversed, cachedDesc);
        cachedDescKey = key;
    }
    return cachedDesc;
}

uint8_t cardDescriptionPageCount(uint8_t cardIdx, bool reversed) {
    const std::vector<DescLine>& lines = getDescription(cardIdx, reversed);
    return (uint8_t)((lines.size() + DESC_LINES_PER_PAGE - 1) / DESC_LINES_PER_PAGE);
}

uint8_t drawCardDescription(uint8_t cardIdx, bool reversed, const char* positionLabel, uint8_t page, bool partial) {
    const TarotCard& card = getCard(cardIdx);
    const std::vector<DescLine>& lines = getDescription(cardIdx, reversed);
    const uint8_t pages = (uint8_t)((lines.size() + DESC_LINES_PER_PAGE - 1) / DESC_LINES_PER_PAGE);
    if (page >= pages) page = 0;

    beginFrame();

    // Top Header: Title and Page
    String header = String(card.name);
    if (positionLabel) header = String(positionLabel) + " - " + header;

    // Spread headers like "Present - Knight of Pentacles" (146 px) are too wide in bold:
    // fall back to the regular font, and only then truncate with an ellipsis.
    u8g2.setFont(FONT_BOLD);
    if (textWidth(header.c_str()) > SCREEN_WIDTH - 2 * PAD) u8g2.setFont(FONT_BODY);
    std::vector<String> hLines;
    wrapText(header.c_str(), SCREEN_WIDTH - 2 * PAD, hLines, 1);
    drawText(PAD, DESC_HEADER_BASELINE, hLines.empty() ? "" : hLines[0].c_str());
    display.drawFastHLine(0, DESC_HEADER_DIV_Y, SCREEN_WIDTH, GxEPD_BLACK);

    // Body Lines
    const size_t start = (size_t)page * DESC_LINES_PER_PAGE;
    int16_t y = DESC_FIRST_BASELINE;
    for (size_t i = start; i < lines.size() && i < start + DESC_LINES_PER_PAGE; i++) {
        u8g2.setFont(lines[i].bold ? FONT_BOLD : FONT_BODY);
        drawText(PAD, y, lines[i].text.c_str());
        y += DESC_LINE_H;
    }

    // Footer: battery level (left) and page number (right)
    display.drawFastHLine(0, DESC_FOOTER_DIV_Y, SCREEN_WIDTH, GxEPD_BLACK);
    char bBuf[16];
    snprintf(bBuf, sizeof(bBuf), "Battery %u%%", batteryPercent());
    u8g2.setFont(FONT_SMALL);
    drawText(PAD, DESC_FOOTER_BASELINE, bBuf);
    if (pages > 1) {
        char pBuf[8];
        snprintf(pBuf, sizeof(pBuf), "%u/%u", page + 1, pages);
        drawText(SCREEN_WIDTH - textWidth(pBuf) - PAD, DESC_FOOTER_BASELINE, pBuf);
    }

    pushScreen(partial);
    return pages;
}

void drawShuffleFrame(int frame, bool firstFrame) {
    const int16_t wx = 0, wy = 30, ww = 120, wh = 190;
    const int16_t cw = 56, ch = 88;

    if (firstFrame) {
        beginFrame();
    } else {
        display.fillRect(wx, wy, ww, wh, GxEPD_WHITE);
    }

    int step = frame % 6;
    int16_t midX = SCREEN_WIDTH / 2;
    int16_t midY = wy + 55;

    // Mini card backs shuffling animation
    int16_t offset = (step == 1 || step == 4) ? 14 : ((step == 2 || step == 5) ? 8 : 0);
    int16_t liftL = (step % 2 == 1) ? -4 : 2;
    int16_t liftR = (step % 2 == 0) ? -4 : 2;

    int16_t leftX = midX - cw / 2 - offset;
    int16_t rightX = midX - cw / 2 + offset;
    int16_t centerX = midX - cw / 2;
    int16_t baseY = midY - ch / 2;

    if (step == 0) {
        drawCardBack(centerX, baseY, cw, ch);
    } else if (step == 1 || step == 2) {
        drawCardBack(leftX, baseY + liftL, cw, ch);
        drawCardBack(rightX, baseY + liftR, cw, ch);
    } else {
        drawCardBack(leftX, baseY + liftL, cw, ch);
        drawCardBack(rightX, baseY + liftR, cw, ch);
        drawCardBack(centerX, baseY, cw, ch);
    }

    u8g2.setFont(FONT_BOLD);
    String text = "Shuffling";
    for (int i = 0; i < (frame % 3) + 1; i++) text += ".";
    drawTextCentered(SCREEN_WIDTH / 2, wy + ch + 38, text.c_str());

    u8g2.setFont(FONT_SMALL);
    drawTextCentered(SCREEN_WIDTH / 2, wy + ch + 52, "Release to draw");

    if (firstFrame) {
        if (!partialPrimed) {
            display.epd2.refresh(true);
            partialPrimed = true;
        }
        display.display(true);
    } else {
        display.displayWindow(wx, wy, ww, wh);
    }
}
