// config.h
// ----------------------------------------------------------------------------
// Memory Game — compile-time configuration
//
// All tunables, GPIO mappings, and game parameters live here. Changing LED
// count, brightness, timings, or box-to-socket mapping should never require
// changes anywhere else.
//
// Hardware: ESP32-S3-WROOM-1 N16R8 on ESP32-S3-DevKitC-1 v1.0
// LEDs:     SK6812 RGBW, driven via 3.3V → 5V level shifter (TXS0108E)
// Buttons:  NO push-buttons to GND, INPUT_PULLUP on ESP32 side
// ----------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Box count
// ---------------------------------------------------------------------------
// 10 boxes arranged as 5 pairs (1A/1B, 2A/2B, ... 5A/5B).
static constexpr uint8_t NUM_BOXES = 10;
static constexpr uint8_t NUM_PAIRS = NUM_BOXES / 2;

// Socket indices (0-based) matching the CNC panel labels.
//   Index:  0   1   2   3   4   5   6   7   8   9
//   Label: 1A  1B  2A  2B  3A  3B  4A  4B  5A  5B
enum Socket : uint8_t {
    SOCKET_1A = 0, SOCKET_1B,
    SOCKET_2A,     SOCKET_2B,
    SOCKET_3A,     SOCKET_3B,
    SOCKET_4A,     SOCKET_4B,
    SOCKET_5A,     SOCKET_5B
};

static const char* const SOCKET_LABELS[NUM_BOXES] = {
    "1A", "1B", "2A", "2B", "3A", "3B", "4A", "4B", "5A", "5B"
};

// ---------------------------------------------------------------------------
// GPIO mapping (index == Socket)
// ---------------------------------------------------------------------------
// Button pins — inputs with INPUT_PULLUP, button connects pin to GND.
static constexpr uint8_t BUTTON_PINS[NUM_BOXES] = {
    /* 1A */ 4,
    /* 1B */ 5,
    /* 2A */ 7,
    /* 2B */ 15,
    /* 3A */ 16,
    /* 3B */ 17,
    /* 4A */ 18,
    /* 4B */ 8,
    /* 5A */ 3,
    /* 5B */ 46
};

// LED data pins — outputs, routed through TXS0108E level shifter to SK6812.
//
// NOTE on pin choices for ESP32-S3-WROOM-1 N16R8:
//   * GPIO 19, 20 reserved for USB D-/D+ (native USB-CDC).
//   * GPIO 26-32 reserved for internal SPI flash.
//   * GPIO 33-37 reserved for octal PSRAM (R8) — do NOT use.
//   * GPIO 43, 44 are U0TXD/U0RXD (UART-USB bridge via CH340).
// Picked pins are all free on N16R8 and physically grouped on the right/bottom
// side of the DevKitC-1 header for tidy wiring.
static constexpr uint8_t LED_PINS[NUM_BOXES] = {
    /* 1A */ 1,
    /* 1B */ 2,
    /* 2A */ 42,
    /* 2B */ 41,
    /* 3A */ 40,
    /* 3B */ 39,
    /* 4A */ 38,
    /* 4B */ 21,
    /* 5A */ 47,
    /* 5B */ 48   // also drives the on-board RGB LED — cosmetic, harmless
};

// ---------------------------------------------------------------------------
// Pair mapping
// ---------------------------------------------------------------------------
// Returns the socket index of the partner in a matching pair.
// Pairs are fixed by construction: (1A,1B), (2A,2B), ...
inline uint8_t partnerOf(uint8_t socket) {
    return socket ^ 0x01;   // toggles the LSB: 0<->1, 2<->3, 4<->5, ...
}

// ---------------------------------------------------------------------------
// LED strip configuration
// ---------------------------------------------------------------------------
// Strips are different physical lengths (72–180 LEDs). To stay hardware-
// agnostic (NFR5) we allocate for the maximum length on every channel. Extra
// pixels on shorter strips simply go nowhere.
static constexpr uint16_t LEDS_PER_STRIP = 180;

// Brightness levels (0-255 scale used by Adafruit_NeoPixel).
// Chosen to stay well inside the 5 V / 30 A PSU budget (NFR1).
static constexpr uint8_t BRIGHTNESS_IDLE_PCT    = 20;   // white idle
static constexpr uint8_t BRIGHTNESS_ACTIVE_MIN  = 0;    // pulse min
static constexpr uint8_t BRIGHTNESS_ACTIVE_MAX  = 40;   // pulse max
static constexpr uint8_t BRIGHTNESS_RESULT_PCT  = 40;   // green / red after match check

// Helper: percent (0-100) → 8-bit intensity.
constexpr uint8_t pct(uint8_t p) { return (uint16_t(p) * 255U) / 100U; }

// ---------------------------------------------------------------------------
// Timings (milliseconds)
// ---------------------------------------------------------------------------
static constexpr uint32_t BUTTON_DEBOUNCE_MS   = 50;      // Bounce2 debounce
static constexpr uint32_t STATE_TIMEOUT_MS     = 10000;   // auto-reset after 10 s
static constexpr uint32_t PULSE_PERIOD_MS      = 1500;    // active pulse cycle length
static constexpr uint32_t POWERON_STEP_MS      = 1000;    // red/green/blue step length
static constexpr uint32_t LED_REFRESH_MS       = 20;      // how often to redraw animations (~50 Hz)

// ---------------------------------------------------------------------------
// Serial / debug
// ---------------------------------------------------------------------------
static constexpr uint32_t SERIAL_BAUD = 115200;

// Set to 1 to allow simulating button presses by typing socket labels
// ("1A", "1B", ..., "5B") into the serial monitor. See section 5.1 of the
// design doc.
#define ENABLE_SERIAL_SIMULATION 1