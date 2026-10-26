// led_control.h
// ----------------------------------------------------------------------------
// Owns the 10 Adafruit_NeoPixel strips and renders per-box views to LEDs.
//
// Public surface is intentionally small: the main loop tells this module
// which view each box shows (see states.h) and calls tick() continuously. All
// colour/animation decisions live here.
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

// Record the desired view for a single box. Cheap; does not push to LEDs.
// Animations (check pulse, win) start when the view changes.
void setBoxView(uint8_t socket, BoxView v);

// Force all 10 boxes to OPEN and redraw them. Also leaves sleep mode.
void setAllOpen();

// Invite style for the other open boxes (runtime switch, not persisted).
void  setStyle(Style s);
Style style();

// Night mode: when sleeping, every strip is switched fully off and tick() only
// re-sends black (one strip per LED_SLEEP_REFRESH_MS) until setSleeping(false)
// / setAllOpen() is called.
void setSleeping(bool sleeping);
bool isSleeping();

// Render animations, push any changed colours to the strips and periodically
// re-send static colours. Call on every loop() iteration, also while sleeping.
void tick();

// Number of strip pushes (show() calls) since boot, for STATUS.
uint32_t showCount();

}  // namespace led
