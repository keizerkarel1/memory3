// states.h
// ----------------------------------------------------------------------------
// What a box should look like (decided by game_logic, drawn by led_control)
// and the selectable invite styles.
// ----------------------------------------------------------------------------
#pragma once

#include <stdint.h>

// One per box; the renderer turns each view into a colour / animation.
enum class BoxView : uint8_t {
    OPEN,       // white 20 % — free to press
    INVITE,     // open box while another box waits for its partner (style-dependent)
    SELECTED,   // solid blue — first of a pair, waiting for the second press
    CHECKING,   // blinking blue — pair pressed, verdict follows
    MATCH,      // solid green — pair found, locked until the game ends
    NO_MATCH,   // solid red — wrong pair, open again after NO_MATCH_MS
    WIN         // all boxes green <-> white — game won
};

// How the other open boxes invite a second press. Order matches DEFAULT_STYLE.
enum class Style : uint8_t {
    WHITE,      // white breathing
    YELLOW,     // warm yellow breathing
    BLINK       // white on / off
};
static constexpr uint8_t NUM_STYLES = 3;

// Convenience labels for logs.
inline const char* viewName(BoxView v) {
    switch (v) {
        case BoxView::OPEN:     return "OPEN";
        case BoxView::INVITE:   return "INVITE";
        case BoxView::SELECTED: return "SELECTED";
        case BoxView::CHECKING: return "CHECKING";
        case BoxView::MATCH:    return "MATCH";
        case BoxView::NO_MATCH: return "NO_MATCH";
        case BoxView::WIN:      return "WIN";
    }
    return "?";
}

inline const char* styleName(Style s) {
    switch (s) {
        case Style::WHITE:  return "WHITE";
        case Style::YELLOW: return "YELLOW";
        case Style::BLINK:  return "BLINK";
    }
    return "?";
}
