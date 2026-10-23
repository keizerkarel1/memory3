// led_control.cpp
// ----------------------------------------------------------------------------
// Implementation detail for led_control.h.
//
// One Adafruit_NeoPixel instance per strip (10 total). On the ESP32-S3 the
// library drives each show() through the RMT peripheral, refilled from an ISR,
// borrowing a channel only for the duration of the call:
//
//   * show() blocks the loop task for ~7 ms per 180-LED GRBW strip, but the
//     CPU is mostly idle meanwhile (the RMT hardware clocks the bits out).
//   * Interrupt latency (e.g. USB-CDC logging) or noise on the long data cable
//     can still truncate or corrupt a frame. Static colours are therefore
//     re-sent periodically, one strip at a time, so a bad frame heals within
//     about a second (LED_STATIC_REFRESH_MS). Animations heal by themselves.
//
// Every tick() computes the wanted colour of each strip and only pushes the
// strips whose colour changed. Animations change colour in steps (breathing
// every INVITE_FRAME_MS, blinks per step), which caps the LED traffic.
// ----------------------------------------------------------------------------
#include "led_control.h"

#include <Adafruit_NeoPixel.h>
#include <math.h>

namespace led {

namespace {

// One strip per box. All sized to LEDS_PER_STRIP (max length). Shorter strips
// just ignore the trailing pixels.
Adafruit_NeoPixel strips[NUM_BOXES] = {
    Adafruit_NeoPixel(LEDS_PER_STRIP, LED_PINS[0], NEO_GRBW + NEO_KHZ800),
    Adafruit_NeoPixel(LEDS_PER_STRIP, LED_PINS[1], NEO_GRBW + NEO_KHZ800),
    Adafruit_NeoPixel(LEDS_PER_STRIP, LED_PINS[2], NEO_GRBW + NEO_KHZ800),
    Adafruit_NeoPixel(LEDS_PER_STRIP, LED_PINS[3], NEO_GRBW + NEO_KHZ800),
    Adafruit_NeoPixel(LEDS_PER_STRIP, LED_PINS[4], NEO_GRBW + NEO_KHZ800),
    Adafruit_NeoPixel(LEDS_PER_STRIP, LED_PINS[5], NEO_GRBW + NEO_KHZ800),
    Adafruit_NeoPixel(LEDS_PER_STRIP, LED_PINS[6], NEO_GRBW + NEO_KHZ800),
    Adafruit_NeoPixel(LEDS_PER_STRIP, LED_PINS[7], NEO_GRBW + NEO_KHZ800),
    Adafruit_NeoPixel(LEDS_PER_STRIP, LED_PINS[8], NEO_GRBW + NEO_KHZ800),
    Adafruit_NeoPixel(LEDS_PER_STRIP, LED_PINS[9], NEO_GRBW + NEO_KHZ800),
};

// Current view per box and when it was entered (animation phase origin).
BoxView  view[NUM_BOXES];
uint32_t viewSince[NUM_BOXES];

// Colour last pushed to each strip, and whether it must be pushed regardless.
uint32_t sent[NUM_BOXES];
bool     force[NUM_BOXES];

Style    currentStyle = static_cast<Style>(DEFAULT_STYLE % NUM_STYLES);

// All inviting boxes breathe in sync; the phase starts when the invite starts.
bool     inviting    = false;
uint32_t inviteEpoch = 0;

// Round-robin periodic refresh (see staggeredRefresh()).
uint32_t lastStaggerMs = 0;
uint8_t  nextRefresh   = 0;

// Night mode flag — while true, tick() only re-sends black to the strips.
bool sleeping = false;

uint32_t shows = 0;

// ---------------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------------

// Same packing as Adafruit_NeoPixel::Color(r, g, b, w).
constexpr uint32_t rgbw(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    return (uint32_t(w) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

// RGBW white comes exclusively from the W channel → much more efficient than
// driving R+G+B together (NFR1).
constexpr uint32_t COLOR_OFF   = 0;
constexpr uint32_t COLOR_OPEN  = rgbw(0, 0, 0, pct(BRIGHTNESS_IDLE_PCT));
constexpr uint32_t COLOR_BLUE  = rgbw(0, 0, pct(BRIGHTNESS_SELECTED_PCT), 0);
constexpr uint32_t COLOR_GREEN = rgbw(0, pct(BRIGHTNESS_RESULT_PCT), 0, 0);
constexpr uint32_t COLOR_RED   = rgbw(pct(BRIGHTNESS_RESULT_PCT), 0, 0, 0);

// Push one colour to a whole strip.
void show(uint8_t socket, uint32_t color) {
    Adafruit_NeoPixel& strip = strips[socket];
    strip.fill(color, 0, LEDS_PER_STRIP);
    strip.show();
    sent[socket]  = color;
    force[socket] = false;
    ++shows;
}

// Breathing level between minPct and maxPct, starting at minPct. 'elapsed' is
// rounded down to INVITE_FRAME_MS so the level only changes once per frame.
uint8_t breathe(uint32_t elapsed, uint8_t minPct, uint8_t maxPct) {
    elapsed -= elapsed % INVITE_FRAME_MS;
    float f    = float(elapsed % INVITE_PERIOD_MS) / INVITE_PERIOD_MS * 2.0f * PI;
    float norm = (1.0f - cosf(f)) * 0.5f;   // 0..1
    uint8_t lo = pct(minPct);
    uint8_t hi = pct(maxPct);
    return lo + static_cast<uint8_t>(norm * (hi - lo) + 0.5f);
}

uint32_t inviteColor(uint32_t elapsed) {
    switch (currentStyle) {
        case Style::WHITE:
            return rgbw(0, 0, 0, breathe(elapsed, INVITE_WHITE_MIN_PCT, INVITE_WHITE_MAX_PCT));
        case Style::YELLOW: {
            uint8_t r = breathe(elapsed, INVITE_YELLOW_MIN_PCT, INVITE_YELLOW_MAX_PCT);
            uint8_t g = (uint16_t(r) * INVITE_YELLOW_G_OF_R) / 100U;
            return rgbw(r, g, 0, 0);
        }
        case Style::BLINK: {
            bool on = (elapsed / INVITE_BLINK_STEP_MS) % 2 == 0;
            return rgbw(0, 0, 0, pct(on ? INVITE_BLINK_ON_PCT : INVITE_BLINK_OFF_PCT));
        }
    }
    return COLOR_OPEN;
}

bool isAnimated(BoxView v) {
    return v == BoxView::INVITE || v == BoxView::CHECKING || v == BoxView::WIN;
}

uint32_t colorFor(uint8_t socket, uint32_t now, uint32_t invite) {
    uint32_t elapsed = now - viewSince[socket];
    switch (view[socket]) {
        case BoxView::OPEN:     return COLOR_OPEN;
        case BoxView::INVITE:   return invite;
        case BoxView::SELECTED: return COLOR_BLUE;
        case BoxView::CHECKING:
            return (elapsed / CHECK_BLINK_STEP_MS) % 2 == 0 ? COLOR_BLUE : COLOR_OFF;
        case BoxView::MATCH:    return COLOR_GREEN;
        case BoxView::NO_MATCH: return COLOR_RED;
        case BoxView::WIN:
            return (elapsed / WIN_STEP_MS) % 2 == 0 ? COLOR_GREEN : COLOR_OPEN;
    }
    return COLOR_OPEN;
}

// Re-send one strip per call, in round-robin order, to repair frames that were
// corrupted on the way to the LEDs. Animated strips (redrawn anyway) and
// strips with a pending push are skipped. Silent on purpose: the Pi logs every
// serial line.
void staggeredRefresh(uint32_t now) {
    uint32_t interval = sleeping ? LED_SLEEP_REFRESH_MS
                                 : LED_STATIC_REFRESH_MS / NUM_BOXES;
    if (now - lastStaggerMs < interval) return;
    lastStaggerMs = now;

    uint8_t i = nextRefresh;
    nextRefresh = (nextRefresh + 1) % NUM_BOXES;

    if (sleeping) {
        show(i, COLOR_OFF);
    } else if (!isAnimated(view[i]) && !force[i]) {
        show(i, sent[i]);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void begin() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < NUM_BOXES; ++i) {
        strips[i].begin();
        strips[i].setBrightness(255);   // we manage brightness via raw values
        strips[i].clear();
        strips[i].show();
        view[i]      = BoxView::OPEN;
        viewSince[i] = now;
        sent[i]      = COLOR_OFF;
        force[i]     = true;
    }
}

void powerOnTest() {
    struct { uint8_t r, g, b; const char* name; } steps[] = {
        {255, 0,   0,   "RED"},
        {0,   255, 0,   "GREEN"},
        {0,   0,   255, "BLUE"},
    };

    for (auto& step : steps) {
        Serial.printf("[POWER-ON] All strips: %s\n", step.name);
        // Scale down the test color so we stay within the PSU budget when all
        // 10 strips are lit full-length.
        uint8_t r = (uint16_t(step.r) * BRIGHTNESS_RESULT_PCT) / 100U;
        uint8_t g = (uint16_t(step.g) * BRIGHTNESS_RESULT_PCT) / 100U;
        uint8_t b = (uint16_t(step.b) * BRIGHTNESS_RESULT_PCT) / 100U;
        for (uint8_t i = 0; i < NUM_BOXES; ++i) {
            show(i, rgbw(r, g, b, 0));
        }
        delay(POWERON_STEP_MS);
    }

    for (uint8_t i = 0; i < NUM_BOXES; ++i) {
        show(i, COLOR_OFF);
    }
}

void setBoxView(uint8_t socket, BoxView v) {
    if (socket >= NUM_BOXES) return;
    if (view[socket] != v) {
        view[socket]      = v;
        viewSince[socket] = millis();
    }
}

void setAllOpen() {
    sleeping = false;
    uint32_t now = millis();
    for (uint8_t i = 0; i < NUM_BOXES; ++i) {
        // Force a redraw even if the recorded view was already OPEN: after a
        // sleep the strips are physically dark.
        view[i]      = BoxView::OPEN;
        viewSince[i] = now;
        force[i]     = true;
    }
}

void setStyle(Style s) {
    currentStyle = s;
}

Style style() {
    return currentStyle;
}

void setSleeping(bool s) {
    if (s == sleeping) return;
    sleeping = s;
    if (sleeping) {
        for (uint8_t i = 0; i < NUM_BOXES; ++i) {
            show(i, COLOR_OFF);
            view[i] = BoxView::OPEN;
        }
    } else {
        setAllOpen();
    }
}

bool isSleeping() {
    return sleeping;
}

void tick() {
    uint32_t now = millis();

#if LED_PERIODIC_REFRESH
    staggeredRefresh(now);
#endif
    if (sleeping) return;   // night mode: strips stay dark

    bool anyInvite = false;
    for (uint8_t i = 0; i < NUM_BOXES; ++i) {
        if (view[i] == BoxView::INVITE) { anyInvite = true; break; }
    }
    if (anyInvite && !inviting) inviteEpoch = now;
    inviting = anyInvite;
    uint32_t invite = anyInvite ? inviteColor(now - inviteEpoch) : COLOR_OPEN;

    for (uint8_t i = 0; i < NUM_BOXES; ++i) {
        uint32_t c = colorFor(i, now, invite);
        if (force[i] || c != sent[i]) show(i, c);
    }
}

uint32_t showCount() {
    return shows;
}

}  // namespace led
