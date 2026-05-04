// button_handler.h
// ----------------------------------------------------------------------------
// Debounced button-press detection for all 10 boxes.
//
// The module reports *edge* events (which button went from not-pressed to
// pressed in this tick), not level. The state machine consumes those events
// and decides what to do.
// ----------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include "config.h"

namespace buttons {

// Initialise pin modes and the Bounce2 instances. Call from setup().
void begin();

// Poll all buttons. Must be called on every loop() iteration.
void tick();

// If a button was pressed (falling edge) since the previous pop, returns
// true and writes the socket index (0-9) into *socket. Otherwise false.
//
// Events are queued (FIFO) so fast presses aren't lost.
bool popPress(uint8_t* socket);

// Injects a simulated press event (used by the serial-simulation path).
void injectPress(uint8_t socket);

}  // namespace buttons