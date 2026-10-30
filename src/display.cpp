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

// -----------------------------------------------------------------------------
// Low-level helpers
// -----------------------------------------------------------------------------

static void pushScreen(bool partial) {
    if (partial && partialCount < FULL_REFRESH_EVERY) {
        display.display(true);
        partialCount++;
    } else {
        display.display(false);
        partialCount = 0;
    }
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

    const int16_t bytesPerRow = (w + 7) / 8;
    for (int16_t y = 0; y < h; y++) {
        for (int16_t x = 0; x < w; x++) {
            uint8_t b = pgm_read_byte(bmp + y * bytesPerRow + (x >> 3));
            if (b & (0x80 >> (x & 7))) {
                int16_t px = reversed ? (w - 1 - x) : x;
                int16_t py = reversed ? (h - 1 - y) : y;
                display.drawPixel(x0 + px, y0 + py, GxEPD_BLACK);
            }
        }
    }
}

// Procedural card back: border, diamond pattern and moon medallion
static void drawCardBack(int16_t x, int16_t y, int16_t w, int16_t h) {
    display.fillRect(x, y, w, h, GxEPD_WHITE);
    display.drawRoundRect(x, y, w, h, 4, GxEPD_BLACK);
    display.drawRoundRect(x + 3, y + 3, w - 6, h - 6, 3, GxEPD_BLACK);

    // Diamond pattern
    const int16_t ix = x + 5, iy = y + 5, iw = w - 10, ih = h - 10;
    const int16_t step = w >= 60 ? 12 : 8;
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
    const int16_t r = w / 4;
    display.fillCircle(cx, cy, r + 2, GxEPD_WHITE);
    display.drawCircle(cx, cy, r + 2, GxEPD_BLACK);
    display.drawCircle(cx, cy, r, GxEPD_BLACK);
    display.fillCircle(cx, cy, r - 2, GxEPD_BLACK);
    display.fillCircle(cx + (r - 2) / 2, cy - (r - 2) / 3, (r - 2) * 7 / 10, GxEPD_WHITE);
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

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
    display.hibernate();
}

void drawSplashScreen(const SplashInfo& info) {
    beginFrame();

    // Decorative Card Back at top
    const int16_t bw = 70, bh = 100;
    const int16_t bx = (SCREEN_WIDTH - bw) / 2;
    drawCardBack(bx, 10, bw, bh);

    u8g2.setFont(FONT_TITLE);
    drawTextCentered(SCREEN_WIDTH / 2, 130, "TAROT");

    u8g2.setFont(FONT_SMALL);
    drawTextCentered(SCREEN_WIDTH / 2, 142, "78 CARDS - LILYGO T5");
    display.drawFastHLine(8, 148, SCREEN_WIDTH - 16, GxEPD_BLACK);

    u8g2.setFont(FONT_BOLD);
    drawText(8, 162, "Mode:");
    u8g2.setFont(FONT_BODY);
    drawText(46, 162, info.mode == MODE_SPREAD ? "3-Card Spread" : "Single Card");

    u8g2.setFont(FONT_BOLD);
    drawText(8, 175, "Deck:");
    u8g2.setFont(FONT_BODY);
    char buf[32];
    snprintf(buf, sizeof(buf), "%u / 78 left", info.cardsLeft);
    drawText(46, 175, buf);

    u8g2.setFont(FONT_BOLD);
    drawText(8, 188, "Last:");
    u8g2.setFont(FONT_BODY);
    if (info.lastReading && info.lastReading->count > 0) {
        String last;
        if (info.lastReading->count == 1) {
            last = getCard(info.lastReading->cards[0]).name;
            last += info.lastReading->isReversed(0) ? " (Down)" : " (Up)";
        } else {
            last = "3-Card Spread";
        }
        std::vector<String> lines;
        wrapText(last.c_str(), SCREEN_WIDTH - 50, lines, 1);
        drawText(46, 188, lines.empty() ? "" : lines[0].c_str());
    } else {
        drawText(46, 188, "-");
    }

    display.drawFastHLine(8, 196, SCREEN_WIDTH - 16, GxEPD_BLACK);

    u8g2.setFont(FONT_BODY);
    drawTextCentered(SCREEN_WIDTH / 2, 212, "BTN 1 : Draw card");
    drawTextCentered(SCREEN_WIDTH / 2, 226, "Hold  : Card details");
    drawTextCentered(SCREEN_WIDTH / 2, 240, "BOOT  : Mode / Last");

    pushScreen(false);
}

// Vertical Card Face
// 1. CARD IMAGE at top (upside down if reversed)
// 2. CARD NAME (Up/ Down status)
// 3. SHORT DESCRIPTION
void drawCardFace(uint8_t cardIdx, bool reversed, const char* positionLabel) {
    const TarotCard& card = getCard(cardIdx);
    beginFrame();

    // 1. CARD IMAGE at the top
    const int16_t cardX = (SCREEN_WIDTH - CARD_IMG_WIDTH) / 2; // 9 px
    const int16_t cardY = 2;
    drawCardBitmap(cardX, cardY, cardIdx, reversed);

    // 2. Divider line below card
    const int16_t divY = cardY + CARD_IMG_HEIGHT + 2; // 166 px
    display.drawFastHLine(4, divY, SCREEN_WIDTH - 8, GxEPD_BLACK);

    // 3. CARD NAME (Up/ Down status)
    String title = String(card.name) + (reversed ? " (Down)" : " (Up)");
    if (positionLabel) {
        title = String(positionLabel) + ": " + title;
    }

    const int16_t textMargin = 4;
    const int16_t maxW = SCREEN_WIDTH - 2 * textMargin; // 114 px

    u8g2.setFont(FONT_BOLD);
    std::vector<String> titleLines;
    wrapText(title.c_str(), maxW, titleLines, 2);

    int16_t y = divY + 11;
    for (const String& l : titleLines) {
        drawText(textMargin, y, l.c_str());
        y += 11;
    }

    // 4. SHORT DESCRIPTION
    const char* descText = reversed ? card.reversed : card.upright;
    u8g2.setFont(FONT_BODY);
    std::vector<String> descLines;
    const int16_t maxDescLines = (SCREEN_HEIGHT - y + 10) / 11;
    wrapText(descText, maxW, descLines, maxDescLines > 0 ? maxDescLines : 1);

    for (const String& l : descLines) {
        drawText(textMargin, y, l.c_str());
        y += 11;
    }

    pushScreen(false);
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
static const int16_t DESC_FIRST_BASELINE = 28;
static const uint8_t DESC_LINES_PER_PAGE = 18;   // fits 18 lines vertically

static void buildDescription(const TarotCard& card, bool reversed, std::vector<DescLine>& out) {
    const int16_t width = SCREEN_WIDTH - 8;
    std::vector<String> tmp;

    out.push_back({reversed ? "Meaning (Reversed)" : "Meaning (Upright)", true});
    u8g2.setFont(FONT_BODY);
    wrapText(reversed ? card.reversed : card.upright, width, tmp);
    for (auto& s : tmp) out.push_back({s, false});
    tmp.clear();

    out.push_back({"Keywords", true});
    wrapText(card.keywords, width, tmp);
    for (auto& s : tmp) out.push_back({s, false});
    tmp.clear();

    out.push_back({reversed ? "Opposite (Upright)" : "Opposite (Reversed)", true});
    wrapText(reversed ? card.upright : card.reversed, width, tmp);
    for (auto& s : tmp) out.push_back({s, false});
}

uint8_t cardDescriptionPageCount(uint8_t cardIdx, bool reversed) {
    std::vector<DescLine> lines;
    buildDescription(getCard(cardIdx), reversed, lines);
    return (uint8_t)((lines.size() + DESC_LINES_PER_PAGE - 1) / DESC_LINES_PER_PAGE);
}

uint8_t drawCardDescription(uint8_t cardIdx, bool reversed, const char* positionLabel, uint8_t page) {
    const TarotCard& card = getCard(cardIdx);
    std::vector<DescLine> lines;
    buildDescription(card, reversed, lines);
    const uint8_t pages = (uint8_t)((lines.size() + DESC_LINES_PER_PAGE - 1) / DESC_LINES_PER_PAGE);
    if (page >= pages) page = 0;

    beginFrame();

    // Top Header: Title and Page
    String header = String(card.name) + (reversed ? " (Down)" : " (Up)");
    if (positionLabel) header = String(positionLabel) + " - " + header;

    u8g2.setFont(FONT_BOLD);
    std::vector<String> hLines;
    wrapText(header.c_str(), SCREEN_WIDTH - 28, hLines, 1);
    drawText(4, 12, hLines.empty() ? "" : hLines[0].c_str());

    if (pages > 1) {
        char pBuf[8];
        snprintf(pBuf, sizeof(pBuf), "%u/%u", page + 1, pages);
        u8g2.setFont(FONT_SMALL);
        drawText(SCREEN_WIDTH - textWidth(pBuf) - 4, 12, pBuf);
    }
    display.drawFastHLine(0, 16, SCREEN_WIDTH, GxEPD_BLACK);

    // Body Lines
    const size_t start = (size_t)page * DESC_LINES_PER_PAGE;
    int16_t y = DESC_FIRST_BASELINE;
    for (size_t i = start; i < lines.size() && i < start + DESC_LINES_PER_PAGE; i++) {
        u8g2.setFont(lines[i].bold ? FONT_BOLD : FONT_BODY);
        drawText(4, y, lines[i].text.c_str());
        y += DESC_LINE_H;
    }

    pushScreen(true);
    return pages;
}

void drawShuffleFrame(int frame, bool firstFrame) {
    const int16_t wx = 12, wy = 60, ww = 98, wh = 120;
    const int16_t cw = 44, ch = 70;

    if (firstFrame) beginFrame();
    else            display.fillRect(wx, wy, ww, wh, GxEPD_WHITE);

    static const int8_t fSpread[] = {0, 4, 8, 4};
    const int16_t s = fSpread[frame % 4];
    const int16_t baseX = wx + 14, baseY = wy + 10;
    const int16_t cut = (frame & 1) ? -5 : 3;

    drawCardBack(baseX, baseY, cw, ch);
    drawCardBack(baseX + 6 + s, baseY + cut, cw, ch);
    drawCardBack(baseX + 12 + 2 * s, baseY, cw, ch);

    u8g2.setFont(FONT_BOLD);
    String text = "Shuffling";
    for (int i = 0; i < (frame % 3) + 1; i++) text += ".";
    drawTextCentered(SCREEN_WIDTH / 2, wy + ch + 30, text.c_str());

    if (firstFrame) {
        display.display(true);
    } else {
        display.displayWindow(wx, wy, ww, wh);
    }
}
