// game_logic.h
// ----------------------------------------------------------------------------
// The game rules, without any hardware: presses go in, a view per box comes
// out. Time is passed in (millis()), so the module also compiles on the PC and
// is covered by the host unit tests in test/host/.
//
// Rules (agreed with De Reuringdienst, October 2026):
//   * 1st press on an open box -> SELECTED (blue); the other open boxes INVITE.
//   * 2nd press on another open box -> both CHECKING (blue pulse) for
//     CHECK_MS, then MATCH (green, locked) or NO_MATCH (red for
//     NO_MATCH_MS, then open again).
//   * All 5 pairs green -> WIN_HOLD_MS, WIN animation for WIN_ANIM_MS, clean board.
//   * Ignored: presses on green / red / the selected box, during the check
//     check pulse and during the win. They still count as activity.
//   * A selection without 2nd press is cancelled after SELECT_TIMEOUT_MS; no
//     press at all for GAME_IDLE_RESET_MS -> clean board.
// ----------------------------------------------------------------------------
#pragma once

#include <stdint.h>
#include "config.h"
#include "states.h"

namespace game {

// Receives one finished log line (no trailing newline).
using LogFn = void (*)(const char* line);

// Clean board. Call once from setup() (and from every host test).
void begin(uint32_t now, LogFn log);

// A (debounced) button press on socket 0..NUM_BOXES-1.
void press(uint8_t socket, uint32_t now);

// Advance timers. Call on every loop() iteration.
void tick(uint32_t now);

// Clean board right now (sleep, serial RESET). Logs only if a game was going.
void reset(uint32_t now, const char* reason);

BoxView view(uint8_t socket);

uint8_t     pairsFound();
const char* phaseName();        // PLAY / WIN_HOLD / WIN_ANIM
const char* selectedLabel();    // "1A".."5B" or "none"

}  // namespace game
