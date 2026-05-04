// led_control.cpp
// ----------------------------------------------------------------------------
// Implementation detail for led_control.h.
//
// One Adafruit_NeoPixel instance per strip (10 total). Adafruit_NeoPixel uses
// CPU bit-banging for timing, which is a pragmatic fit for this project:
//
//   * ESP32-S3 only has 4 RMT TX channels — not enough for 10 DMA strips.
//   * We update LEDs only on state changes plus a slow ~50 Hz refresh for the
//     blue pulse, so the ~5 ms blocking show() per 180-LED strip is fine.
//   * Interrupt jitter from Wi-Fi is irrelevant (Wi-Fi is unused).
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

// Current state per box.
BoxState currentState[NUM_BOXES] = {
    BoxState::IDLE, BoxState::IDLE, BoxState::IDLE, BoxState::IDLE, BoxState::IDLE,
    BoxState::IDLE, BoxState::IDLE, BoxState::IDLE, BoxState::IDLE, BoxState::IDLE,
};

// Last intensity rendered for a pulsing box (used to skip no-op updates).
uint8_t lastPulseLevel[NUM_BOXES] = {0};

// Whether the strip currently needs a push on next tick (e.g. after a state
// change). Separate from animated pulse updates.
bool dirty[NUM_BOXES] = {true, true, true, true, true, true, true, true, true, true};

uint32_t lastRefreshMs = 0;

// ---------------------------------------------------------------------------
// Low-level fill helpers.
// ---------------------------------------------------------------------------

// Fill a whole strip with a single RGBW value and push it to the hardware.
void fillAndShow(uint8_t socket, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    Adafruit_NeoPixel& strip = strips[socket];
    uint32_t color = strip.Color(r, g, b, w);
    for (uint16_t i = 0; i < LEDS_PER_STRIP; ++i) {
        strip.setPixelColor(i, color);
    }
    strip.show();
}

// Render the color for a state into a strip. 'phase' is only used for ACTIVE
// (0..255 cycles through the blue pulse).
void render(uint8_t socket, BoxState s, uint8_t phase = 0) {
    switch (s) {
        case BoxState::IDLE:
            // RGBW white comes exclusively from the W channel → much more
            // efficient than driving R+G+B together (NFR1).
            fillAndShow(socket, 0, 0, 0, pct(BRIGHTNESS_IDLE_PCT));
            break;

        case BoxState::ACTIVE: {
            // Smooth sinusoidal pulse between MIN and MAX brightness.
            float f = (phase / 255.0f) * 2.0f * PI;
            float norm = (1.0f - cosf(f)) * 0.5f;   // 0..1
            uint8_t level = BRIGHTNESS_ACTIVE_MIN +
                            static_cast<uint8_t>(norm *
                                (BRIGHTNESS_ACTIVE_MAX - BRIGHTNESS_ACTIVE_MIN));
            // Blue only.
            uint8_t b8 = pct(level);
            fillAndShow(socket, 0, 0, b8, 0);
            break;
        }

        case BoxState::MATCH:
            fillAndShow(socket, 0, pct(BRIGHTNESS_RESULT_PCT), 0, 0);
            break;

        case BoxState::NO_MATCH:
            fillAndShow(socket, pct(BRIGHTNESS_RESULT_PCT), 0, 0, 0);
            break;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void begin() {
    for (uint8_t i = 0; i < NUM_BOXES; ++i) {
        strips[i].begin();
        strips[i].setBrightness(255);   // we manage brightness via raw values
        strips[i].clear();
        strips[i].show();
        currentState[i] = BoxState::IDLE;
        dirty[i] = true;
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
            fillAndShow(i, r, g, b, 0);
        }
        delay(POWERON_STEP_MS);
    }

    for (uint8_t i = 0; i < NUM_BOXES; ++i) {
        fillAndShow(i, 0, 0, 0, 0);
    }
}

void setBoxState(uint8_t socket, BoxState s) {
    if (socket >= NUM_BOXES) return;
    if (currentState[socket] != s) {
        currentState[socket] = s;
        dirty[socket] = true;
    }
}

void setAllIdle() {
    for (uint8_t i = 0; i < NUM_BOXES; ++i) {
        setBoxState(i, BoxState::IDLE);
    }
}

void tick() {
    uint32_t now = millis();

    // Throttle refresh so we don't hammer the CPU with bit-banging (which
    // disables interrupts during each show()).
    if (now - lastRefreshMs < LED_REFRESH_MS) {
        // Still process non-pulse dirty flags immediately.
        bool anyDirty = false;
        for (uint8_t i = 0; i < NUM_BOXES; ++i) {
            if (dirty[i] && currentState[i] != BoxState::ACTIVE) {
                anyDirty = true;
                break;
            }
        }
        if (!anyDirty) return;
    }
    lastRefreshMs = now;

    for (uint8_t i = 0; i < NUM_BOXES; ++i) {
        BoxState s = currentState[i];

        if (s == BoxState::ACTIVE) {
            // Map wall-clock time to a 0..255 phase over PULSE_PERIOD_MS.
            uint32_t t = now % PULSE_PERIOD_MS;
            uint8_t phase = static_cast<uint8_t>((t * 255U) / PULSE_PERIOD_MS);
            // Skip redundant writes when phase hasn't changed noticeably.
            if (!dirty[i] && phase == lastPulseLevel[i]) continue;
            lastPulseLevel[i] = phase;
            render(i, s, phase);
            dirty[i] = false;
        } else if (dirty[i]) {
            render(i, s);
            dirty[i] = false;
        }
    }
}

}  // namespace led