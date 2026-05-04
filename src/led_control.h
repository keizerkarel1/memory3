// led_control.h
// ----------------------------------------------------------------------------
// Owns the 10 Adafruit_NeoPixel strips and renders per-box state to LEDs.
//
// Public surface is intentionally small: the state machine tells this module
// which state each box is in and calls tick() at ~50 Hz. All color/animation
// decisions live here.
// ----------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include "config.h"
#include "states.h"

namespace led {

// Initialise all 10 strips and clear them to OFF. Call once from setup().
void begin();

// Run the power-on self-test: cycle every strip through RED -> GREEN -> BLUE
// (POWERON_STEP_MS each), then leave all strips OFF. Blocks for ~3 s.
void powerOnTest();

// Record the desired state for a single box. Cheap; does not push to LEDs.
void setBoxState(uint8_t socket, BoxState s);

// Force all 10 boxes to IDLE.
void setAllIdle();

// Render animations and push any changes to the LED strips. Call from loop().
// Must be called frequently (at least every LED_REFRESH_MS ms).
void tick();

}  // namespace led