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

// Plain <stdint.h> instead of <Arduino.h>: game_logic.cpp includes this file
// and is also compiled on the PC for the host unit tests.
#include <stdint.h>

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
// Rule of thumb: never exceed the power-on test (all 10 strips at 40 % on one
// channel), which is proven on site.
static constexpr uint8_t BRIGHTNESS_IDLE_PCT     = 20;  // white open box
static constexpr uint8_t BRIGHTNESS_SELECTED_PCT = 40;  // solid blue first press
static constexpr uint8_t BRIGHTNESS_CHECK_MIN_PCT = 5;  // blue pulse of the checked pair (2 strips)
static constexpr uint8_t BRIGHTNESS_CHECK_MAX_PCT = 60;
static constexpr uint8_t BRIGHTNESS_RESULT_PCT   = 40;  // green / red after match check, win green

// Helper: percent (0-100) → 8-bit intensity.
constexpr uint8_t pct(uint8_t p) { return (uint16_t(p) * 255U) / 100U; }

// ---------------------------------------------------------------------------
// Timings (milliseconds)
// ---------------------------------------------------------------------------
static constexpr uint32_t BUTTON_DEBOUNCE_MS   = 50;      // Bounce2 debounce
static constexpr uint32_t POWERON_STEP_MS      = 1000;    // red/green/blue step length

// Game rules (see game_logic.h). Chosen from 71 days of press logs: 1st->2nd
// press median 2.9 s / p95 8.3 s, gaps of 30-60 s between presses are rare.
static constexpr uint32_t SELECT_TIMEOUT_MS    = 15000;   // blue box without 2nd press -> open again
static constexpr uint32_t CHECK_MS             = 900;     // both boxes pulse blue before the verdict
static constexpr uint8_t  CHECK_PULSES         = 2;       // pulses within CHECK_MS
static constexpr uint32_t CHECK_FRAME_MS       = 20;      // pulse redraw interval (2 strips only)
static constexpr uint32_t NO_MATCH_MS          = 2500;    // red, then open again
static constexpr uint32_t WIN_HOLD_MS          = 1000;    // last pair green before the win animation
static constexpr uint32_t WIN_ANIM_MS          = 4000;    // all boxes green <-> white
static constexpr uint32_t WIN_STEP_MS          = 500;
static constexpr uint32_t GAME_IDLE_RESET_MS   = 60000;   // no press at all -> clean board

// ---------------------------------------------------------------------------
// Invite styles: how the other open boxes ask for a second press
// ---------------------------------------------------------------------------
// Switchable at runtime with the serial command "STYLE <name>" (not persisted:
// after a reset the ESP falls back to DEFAULT_STYLE). 0 = WHITE, 1 = YELLOW,
// 2 = BLINK; see the extra build env in platformio.ini.
#ifndef DEFAULT_STYLE
#define DEFAULT_STYLE 0
#endif

static constexpr uint32_t INVITE_PERIOD_MS     = 2000;    // breathing cycle (WHITE / YELLOW)
static constexpr uint32_t INVITE_FRAME_MS      = 80;      // breathing redraw interval (~12 Hz)
static constexpr uint32_t INVITE_BLINK_STEP_MS = 500;     // BLINK on / off length

static constexpr uint8_t INVITE_WHITE_MIN_PCT  = 8;       // W channel
static constexpr uint8_t INVITE_WHITE_MAX_PCT  = 35;
static constexpr uint8_t INVITE_YELLOW_MIN_PCT = 5;       // R channel; G follows R
static constexpr uint8_t INVITE_YELLOW_MAX_PCT = 28;
static constexpr uint8_t INVITE_YELLOW_G_OF_R  = 45;      // G = R * 45 % -> warm yellow, R+G peak ~40 %
static constexpr uint8_t INVITE_BLINK_ON_PCT   = 35;      // W channel
static constexpr uint8_t INVITE_BLINK_OFF_PCT  = 5;

// Static colours (white / blue / green / red) are re-sent periodically so a frame
// corrupted on the cable heals within about a second. Strips are refreshed one
// at a time (every LED_STATIC_REFRESH_MS / NUM_BOXES ms), so each pass blocks
// for a single ~7 ms show() instead of all ten.
static constexpr uint32_t LED_STATIC_REFRESH_MS = 1000;   // each static strip re-sent ~1x/s
static constexpr uint32_t LED_SLEEP_REFRESH_MS  = 1000;   // night: one strip re-blacked per second

// Build with -DLED_PERIODIC_REFRESH=0 to disable the refresh above (e.g. to
// verify a hardware fix for corrupted frames without the software safety net).
#ifndef LED_PERIODIC_REFRESH
#define LED_PERIODIC_REFRESH 1
#endif

// ---------------------------------------------------------------------------
// Night mode (serial SLEEP / WAKE, sent by the Raspberry Pi scheduler)
// ---------------------------------------------------------------------------
// The ESP32 has no clock, so the Pi tells it when the museum closes ("SLEEP")
// and opens ("WAKE"). If the WAKE never arrives (Pi down, cable out) the ESP
// wakes itself after SLEEP_MAX_S — failing "on" is the safe failure mode in a
// museum. "SLEEP <seconds>" overrides the default, clamped to SLEEP_MAX_S_CAP.
static constexpr uint32_t SLEEP_MAX_S      = 57600;   // 16 h default auto-wake
static constexpr uint32_t SLEEP_MAX_S_CAP  = 86400;   // never sleep longer than 24 h

// ---------------------------------------------------------------------------
// Serial / debug
// ---------------------------------------------------------------------------
static constexpr uint32_t SERIAL_BAUD = 115200;

// Longest accepted serial command line ("SLEEP 57600" = 11 chars + NUL).
static constexpr uint8_t SERIAL_CMD_BUF = 24;

// Set to 1 to allow simulating button presses by typing socket labels
// ("1A", "1B", ..., "5B") into the serial monitor. See section 5.1 of the
// design doc. The night-mode commands (SLEEP / WAKE / STATUS) are always
// accepted, independent of this flag.
#define ENABLE_SERIAL_SIMULATION 1