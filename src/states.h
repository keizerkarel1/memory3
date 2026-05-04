// states.h
// ----------------------------------------------------------------------------
// Per-box state definitions for the Memory Game state machine.
// ----------------------------------------------------------------------------
#pragma once

#include <Arduino.h>

// Each of the 10 boxes lives in exactly one of these states at any time.
enum class BoxState : uint8_t {
    IDLE,       // white, 20 % — waiting for a press
    ACTIVE,     // blue pulse — first of a pair, waiting for second press
    MATCH,      // solid green — correct pair found
    NO_MATCH    // solid red — wrong pair, will time out
};

// Convenience label for logs.
inline const char* stateName(BoxState s) {
    switch (s) {
        case BoxState::IDLE:     return "IDLE";
        case BoxState::ACTIVE:   return "ACTIVE";
        case BoxState::MATCH:    return "MATCH";
        case BoxState::NO_MATCH: return "NO_MATCH";
    }
    return "?";
}