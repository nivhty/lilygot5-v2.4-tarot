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

// Full frame buffer (128x250 / 8 = 4 KB) – single page, so we can draw and then display()
static GxEPD2_BW<PanelDriver, PanelDriver::HEIGHT> display(
    PanelDriver(/*CS*/ EPD_CS, /*DC*/ EPD_DC, /*RST*/ EPD_RST, /*BUSY*/ EPD_BUSY));
static U8G2_FOR_ADAFRUIT_GFX u8g2;

static uint8_t partialCount = 0;

// Fonts
#define FONT_TITLE  u8g2_font_helvB14_tf
#define FONT_NAME   u8g2_font_helvB12_tf
#define FONT_BOLD   u8g2_font_helvB10_tf
#define FONT_BODY   u8g2_font_helvR10_tf
#define FONT_SMALL  u8g2_font_helvR08_tf
#define FONT_SMALLB u8g2_font_helvB08_tf

static const char* const SUIT_NAMES[] = {"Major Arcana", "Wands", "Cups", "Swords", "Pentacles"};
static const char* const ROMAN[] = {
    "0", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X",
    "XI", "XII", "XIII", "XIV", "XV", "XVI", "XVII", "XVIII", "XIX", "XX", "XXI"};
static const int16_t SPREAD_X[SPREAD_SIZE] = {0, 89, 178};
static const char* const SPREAD_LABELS[SPREAD_SIZE] = {"Past", "Present", "Future"};

// -----------------------------------------------------------------------------
// Low-level helpers
// -----------------------------------------------------------------------------

// Push the frame buffer to the panel. Partial updates are used when requested, with a
// forced full refresh every FULL_REFRESH_EVERY partials to clear ghosting.
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

static void drawTextRight(int16_t rightX, int16_t baseline, const char* s) {
    drawText(rightX - textWidth(s), baseline, s);
}

static void drawTextCentered(int16_t centerX, int16_t baseline, const char* s) {
    drawText(centerX - textWidth(s) / 2, baseline, s);
}

// Word-wrap `text` with the current font into lines no wider than maxWidth.
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

static void drawBattery(int16_t x, int16_t y, uint8_t pct) {
    display.drawRect(x, y, 18, 9, GxEPD_BLACK);
    display.fillRect(x + 18, y + 2, 2, 5, GxEPD_BLACK);
    int16_t fill = (int16_t)(14 * (uint16_t)pct / 100);
    if (fill > 0) display.fillRect(x + 2, y + 2, fill, 5, GxEPD_BLACK);
}

// Draw a 1-bit card bitmap, optionally rotated 180° for reversed cards.
static void drawCardBitmap(int16_t x0, int16_t y0, uint8_t cardIdx, bool reversed) {
    const uint8_t* bmp = cardIdx < TAROT_TOTAL_CARDS ? tarotBitmaps[cardIdx] : nullptr;
    const int16_t w = TAROT_BMP_WIDTH, h = TAROT_BMP_HEIGHT;

    if (!bmp) {
        display.drawRect(x0, y0, w, h, GxEPD_BLACK);
        u8g2.setFont(FONT_SMALL);
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

// Procedural card back: double frame, diamond lattice and a central sun/moon medallion.
static void drawCardBack(int16_t x, int16_t y, int16_t w, int16_t h) {
    display.fillRect(x, y, w, h, GxEPD_WHITE);
    display.drawRoundRect(x, y, w, h, 4, GxEPD_BLACK);
    display.drawRoundRect(x + 3, y + 3, w - 6, h - 6, 3, GxEPD_BLACK);

    // Diamond lattice
    const int16_t ix = x + 6, iy = y + 6, iw = w - 12, ih = h - 12;
    const int16_t step = w >= 60 ? 12 : 8;
    for (int16_t d = -ih; d < iw; d += step) {
        for (int16_t t = 0; t < ih; t++) {
            int16_t px1 = ix + d + t;
            int16_t px2 = ix + iw - 1 - (d + t);
            if (px1 >= ix && px1 < ix + iw && (t & 1) == 0) display.drawPixel(px1, iy + t, GxEPD_BLACK);
            if (px2 >= ix && px2 < ix + iw && (t & 1) == 0) display.drawPixel(px2, iy + t, GxEPD_BLACK);
        }
    }

    // Medallion
    const int16_t cx = x + w / 2, cy = y + h / 2;
    const int16_t r = w / 4;
    display.fillCircle(cx, cy, r + 3, GxEPD_WHITE);
    display.drawCircle(cx, cy, r + 3, GxEPD_BLACK);
    display.drawCircle(cx, cy, r, GxEPD_BLACK);
    // Crescent moon inside
    display.fillCircle(cx, cy, r - 3, GxEPD_BLACK);
    display.fillCircle(cx + (r - 3) / 2, cy - (r - 3) / 3, (r - 3) * 7 / 10, GxEPD_WHITE);
}

static void drawOrientationIcon(int16_t x, int16_t baseline, bool reversed) {
    // Small filled triangle: up = upright, down = reversed
    const int16_t top = baseline - 7;
    if (reversed) display.fillTriangle(x, top, x + 7, top, x + 3, top + 6, GxEPD_BLACK);
    else          display.fillTriangle(x + 3, top, x, top + 6, x + 7, top + 6, GxEPD_BLACK);
}

static String arcanaInfo(const TarotCard& card) {
    if (card.suit == SUIT_MAJOR) {
        return String(card.number <= 21 ? ROMAN[card.number] : "?") + "  Major Arcana";
    }
    return String("Minor Arcana - ") + SUIT_NAMES[card.suit <= SUIT_PENTACLES ? card.suit : 0];
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

void setupDisplay() {
    SPI.begin(EPD_SCLK, EPD_MISO, EPD_MOSI);
    // initial=true: controller RAM is lost while the panel is unpowered, so the
    // first update after boot/wake must be a full refresh.
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

    drawCardBack(0, 0, CARD_IMG_WIDTH, CARD_IMG_HEIGHT);

    const int16_t x0 = 82;
    const int16_t right = SCREEN_WIDTH - 4;

    u8g2.setFont(FONT_TITLE);
    drawText(x0, 20, "TAROT");
    drawBattery(right - 20, 6, info.batteryPct);

    u8g2.setFont(FONT_SMALL);
    drawText(x0, 33, "78 cards  -  LilyGO T5");
    display.drawFastHLine(x0, 38, right - x0, GxEPD_BLACK);

    char buf[48];
    u8g2.setFont(FONT_SMALLB);
    drawText(x0, 51, "Mode:");
    u8g2.setFont(FONT_SMALL);
    drawText(x0 + 34, 51, info.mode == MODE_SPREAD ? "Past / Present / Future" : "Single card");

    u8g2.setFont(FONT_SMALLB);
    drawText(x0, 63, "Deck:");
    u8g2.setFont(FONT_SMALL);
    snprintf(buf, sizeof(buf), "%u / 78 cards left", info.cardsLeft);
    drawText(x0 + 34, 63, buf);

    u8g2.setFont(FONT_SMALLB);
    drawText(x0, 75, "Last:");
    u8g2.setFont(FONT_SMALL);
    if (info.lastReading && info.lastReading->count > 0) {
        String last;
        if (info.lastReading->count == 1) {
            last = getCard(info.lastReading->cards[0]).name;
            if (info.lastReading->isReversed(0)) last += " (R)";
        } else {
            last = "3-card spread";
        }
        std::vector<String> lines;
        wrapText(last.c_str(), right - (x0 + 34), lines, 1);
        drawText(x0 + 34, 75, lines.empty() ? "" : lines[0].c_str());
    } else {
        drawText(x0 + 34, 75, "-");
    }

    display.drawFastHLine(x0, 83, right - x0, GxEPD_BLACK);
    drawText(x0, 96, "BTN: draw    hold: read");
    drawText(x0, 108, "BOOT: last    hold: mode");

    pushScreen(false);
}

void drawCardFace(uint8_t cardIdx, bool reversed, const char* positionLabel, uint8_t batteryPct) {
    const TarotCard& card = getCard(cardIdx);
    beginFrame();

    drawCardBitmap(0, 0, cardIdx, reversed);

    const int16_t x0 = 80;
    const int16_t right = SCREEN_WIDTH - 3;
    const int16_t textW = right - x0;
    int16_t y = 0;

    // Position label (spread) above the name
    if (positionLabel) {
        u8g2.setFont(FONT_SMALLB);
        String label = String(positionLabel);
        label.toUpperCase();
        drawText(x0, 10, label.c_str());
        y = 12;
    }

    // Card name (max 2 lines)
    u8g2.setFont(FONT_NAME);
    std::vector<String> nameLines;
    wrapText(card.name, textW, nameLines, 2);
    for (const String& l : nameLines) {
        y += 15;
        drawText(x0, y, l.c_str());
    }

    // Orientation
    y += 14;
    drawOrientationIcon(x0, y, reversed);
    u8g2.setFont(FONT_SMALL);
    drawText(x0 + 11, y, reversed ? "Reversed" : "Upright");

    y += 5;
    display.drawFastHLine(x0, y, textW, GxEPD_BLACK);

    // Keywords
    u8g2.setFont(FONT_BODY);
    std::vector<String> kwLines;
    const int16_t bottomLineY = SCREEN_HEIGHT - 4;
    const int16_t availLines = (bottomLineY - 12 - y) / 13;
    wrapText(card.keywords, textW, kwLines, availLines > 0 ? availLines : 1);
    for (const String& l : kwLines) {
        y += 13;
        drawText(x0, y, l.c_str());
    }

    // Bottom row: arcana info + battery icon
    u8g2.setFont(FONT_SMALL);
    String info = arcanaInfo(card);
    drawText(x0, bottomLineY, info.c_str());
    drawBattery(right - 20, bottomLineY - 8, batteryPct);

    pushScreen(false);
}

void drawSpread(const Reading& reading) {
    beginFrame();

    for (uint8_t i = 0; i < reading.count && i < SPREAD_SIZE; i++) {
        const int16_t x = SPREAD_X[i];
        const bool rev = reading.isReversed(i);
        drawCardBitmap(x, 0, reading.cards[i], rev);

        // Label tag placed on the edge opposite the card's printed title
        // (title is at the bottom of the art, or at the top when reversed)
        u8g2.setFont(FONT_SMALLB);
        String label = String(SPREAD_LABELS[i]) + (rev ? " (R)" : "");
        const int16_t tw = textWidth(label.c_str()) + 6;
        const int16_t tx = x + (CARD_IMG_WIDTH - tw) / 2;
        const int16_t ty = rev ? SCREEN_HEIGHT - 13 : 0;
        display.fillRect(tx, ty, tw, 13, GxEPD_WHITE);
        display.drawRect(tx, ty, tw, 13, GxEPD_BLACK);
        drawText(tx + 3, ty + 10, label.c_str());
    }

    pushScreen(false);
}

// Build the full list of description lines (shared by layout + rendering)
struct DescLine {
    String text;
    bool bold;
};

static const int16_t DESC_LINE_H = 13;
static const int16_t DESC_FIRST_BASELINE = 30;
static const uint8_t DESC_LINES_PER_PAGE = 7;   // baselines 30..108

static void buildDescription(const TarotCard& card, bool reversed, std::vector<DescLine>& out) {
    const int16_t width = SCREEN_WIDTH - 8;
    std::vector<String> tmp;

    u8g2.setFont(FONT_BODY);
    wrapText(reversed ? card.reversed : card.upright, width, tmp);
    for (auto& s : tmp) out.push_back({s, false});
    tmp.clear();

    out.push_back({"Keywords", true});
    u8g2.setFont(FONT_BODY);
    wrapText(card.keywords, width, tmp);
    for (auto& s : tmp) out.push_back({s, false});
    tmp.clear();

    out.push_back({reversed ? "If upright" : "If reversed", true});
    u8g2.setFont(FONT_BODY);
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

    // Header: [orientation icon] Name ............ Past  1/2
    const int16_t right = SCREEN_WIDTH - 4;
    char meta[24] = "";
    if (pages > 1) snprintf(meta, sizeof(meta), "%u/%u", page + 1, pages);
    String rightText = positionLabel ? String(positionLabel) + (pages > 1 ? "  " : "") + meta : String(meta);

    u8g2.setFont(FONT_SMALL);
    const int16_t metaW = rightText.length() ? textWidth(rightText.c_str()) + 6 : 0;
    drawTextRight(right, 12, rightText.c_str());

    drawOrientationIcon(4, 12, reversed);
    u8g2.setFont(FONT_BOLD);
    std::vector<String> header;
    wrapText(card.name, right - 16 - metaW, header, 1);
    drawText(16, 12, header.empty() ? "" : header[0].c_str());
    display.drawFastHLine(0, 16, SCREEN_WIDTH, GxEPD_BLACK);

    // Body
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
    // Animation window (centre of the screen)
    const int16_t wx = 40, wy = 16, ww = 176, wh = 90;
    const int16_t cw = 36, ch = 61;   // half-size card backs

    if (firstFrame) beginFrame();
    else            display.fillRect(wx, wy, ww, wh, GxEPD_WHITE);

    // Three cards fanning in and out, middle card "cut" up and down
    static const int8_t spread[] = {0, 6, 12, 6};
    const int16_t s = spread[frame % 4];
    const int16_t baseX = wx + 16, baseY = wy + (wh - ch) / 2;
    const int16_t cut = (frame & 1) ? -8 : 4;
    drawCardBack(baseX, baseY, cw, ch);
    drawCardBack(baseX + 10 + s, baseY + cut, cw, ch);
    drawCardBack(baseX + 20 + 2 * s, baseY, cw, ch);

    u8g2.setFont(FONT_BOLD);
    String text = "Shuffling";
    for (int i = 0; i < (frame % 3) + 1; i++) text += ".";
    drawText(wx + 92, wy + wh / 2 + 5, text.c_str());

    if (firstFrame) {
        display.display(true);   // full-window partial: no black flash
    } else {
        display.displayWindow(wx, wy, ww, wh);
    }
}
