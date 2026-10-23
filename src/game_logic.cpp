// game_logic.cpp
// ----------------------------------------------------------------------------
// Implementation detail for game_logic.h. All time arithmetic is unsigned
// "now - since >= duration", which stays correct across the millis() wrap.
// ----------------------------------------------------------------------------
#include "game_logic.h"

#include <stdarg.h>
#include <stdio.h>

namespace game {

namespace {

enum class Box : uint8_t { OPEN, SELECTED, CHECKING, MATCH, NO_MATCH };
enum class Phase : uint8_t { PLAY, WIN_HOLD, WIN_ANIM };

constexpr uint8_t NONE = 0xFF;

Box      box[NUM_BOXES];
uint32_t since[NUM_BOXES];      // millis() when box[] last changed

Phase    phase   = Phase::PLAY;
uint32_t phaseAt = 0;

uint8_t  selected = NONE;       // box waiting for its partner

uint8_t  checkA = NONE;         // pair currently blinking before the verdict
uint8_t  checkB = NONE;
bool     checkMatched = false;
uint32_t checkAt = 0;

bool     started     = false;   // a game is going (first press on a clean board)
uint32_t startedAt   = 0;
uint32_t lastPressAt = 0;       // any press, also ignored ones
uint8_t  pairs       = 0;
uint16_t attempts    = 0;

LogFn    logFn = nullptr;

void logf(const char* fmt, ...) {
    if (!logFn) return;
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    logFn(buf);
}

void setBox(uint8_t socket, Box b, uint32_t now) {
    box[socket]   = b;
    since[socket] = now;
}

void clearBoard(uint32_t now) {
    for (uint8_t i = 0; i < NUM_BOXES; ++i) setBox(i, Box::OPEN, now);
    phase    = Phase::PLAY;
    phaseAt  = now;
    selected = checkA = checkB = NONE;
    started  = false;
    pairs    = 0;
    attempts = 0;
}

// The blink is over: show the verdict and count the pair.
void finishCheck(uint32_t now) {
    uint8_t a = checkA;
    uint8_t b = checkB;
    checkA = checkB = NONE;

    Box result = checkMatched ? Box::MATCH : Box::NO_MATCH;
    setBox(a, result, now);
    setBox(b, result, now);
    if (!checkMatched) return;

    ++pairs;
    logf("[GAME] PAIR %u/%u", pairs, NUM_PAIRS);
    if (pairs >= NUM_PAIRS) {
        logf("[GAME] WON t=%lus attempts=%u",
             (unsigned long)((now - startedAt) / 1000UL), attempts);
        phase   = Phase::WIN_HOLD;
        phaseAt = now;
    }
}

}  // namespace

void begin(uint32_t now, LogFn log) {
    logFn       = log;
    lastPressAt = now;
    clearBoard(now);
}

void press(uint8_t socket, uint32_t now) {
    if (socket >= NUM_BOXES) return;
    lastPressAt = now;
    const char* label = SOCKET_LABELS[socket];

    if (phase != Phase::PLAY) {
        logf("[IGNORE] %s (win)", label);
        return;
    }
    if (checkA != NONE) {
        logf("[IGNORE] %s (checking)", label);
        return;
    }
    switch (box[socket]) {
        case Box::MATCH:    logf("[IGNORE] %s (in MATCH)", label);       return;
        case Box::NO_MATCH: logf("[IGNORE] %s (in NO_MATCH)", label);    return;
        case Box::SELECTED: logf("[IGNORE] %s (already waiting)", label); return;
        default: break;
    }

    if (!started) {
        started   = true;
        startedAt = now;
        logf("[GAME] START");
    }

    // First press: this box waits for a partner.
    if (selected == NONE) {
        selected = socket;
        setBox(socket, Box::SELECTED, now);
        logf("[SELECT] %s", label);
        return;
    }

    // Second press: blink both, verdict in finishCheck().
    checkA       = selected;
    checkB       = socket;
    selected     = NONE;
    checkMatched = (partnerOf(checkA) == checkB);
    checkAt      = now;
    ++attempts;
    setBox(checkA, Box::CHECKING, now);
    setBox(checkB, Box::CHECKING, now);
    logf("[CHECK] %s + %s -> %s", SOCKET_LABELS[checkA], label,
         checkMatched ? "MATCH" : "NO_MATCH");
}

void tick(uint32_t now) {
    if (phase == Phase::WIN_HOLD) {
        if (now - phaseAt >= WIN_HOLD_MS) {
            phase   = Phase::WIN_ANIM;
            phaseAt = now;
        }
        return;
    }
    if (phase == Phase::WIN_ANIM) {
        if (now - phaseAt >= WIN_ANIM_MS) {
            clearBoard(now);
            logf("[GAME] READY");
        }
        return;
    }

    if (checkA != NONE && now - checkAt >= CHECK_BLINK_MS) {
        finishCheck(now);
        if (phase != Phase::PLAY) return;
    }

    for (uint8_t i = 0; i < NUM_BOXES; ++i) {
        if (box[i] == Box::NO_MATCH && now - since[i] >= NO_MATCH_MS) {
            setBox(i, Box::OPEN, now);
        }
    }

    if (selected != NONE && now - since[selected] >= SELECT_TIMEOUT_MS) {
        logf("[CANCEL] %s timeout", SOCKET_LABELS[selected]);
        setBox(selected, Box::OPEN, now);
        selected = NONE;
    }

    if (started && checkA == NONE && now - lastPressAt >= GAME_IDLE_RESET_MS) {
        reset(now, "idle");
    }
}

void reset(uint32_t now, const char* reason) {
    if (started || phase != Phase::PLAY) {
        logf("[GAME] RESET %s pairs=%u/%u attempts=%u",
             reason, pairs, NUM_PAIRS, attempts);
    }
    clearBoard(now);
}

BoxView view(uint8_t socket) {
    if (socket >= NUM_BOXES) return BoxView::OPEN;
    if (phase == Phase::WIN_ANIM) return BoxView::WIN;
    switch (box[socket]) {
        case Box::OPEN:     return selected != NONE ? BoxView::INVITE : BoxView::OPEN;
        case Box::SELECTED: return BoxView::SELECTED;
        case Box::CHECKING: return BoxView::CHECKING;
        case Box::MATCH:    return BoxView::MATCH;
        case Box::NO_MATCH: return BoxView::NO_MATCH;
    }
    return BoxView::OPEN;
}

uint8_t pairsFound() {
    return pairs;
}

const char* phaseName() {
    switch (phase) {
        case Phase::PLAY:     return "PLAY";
        case Phase::WIN_HOLD: return "WIN_HOLD";
        case Phase::WIN_ANIM: return "WIN_ANIM";
    }
    return "?";
}

const char* selectedLabel() {
    return selected == NONE ? "none" : SOCKET_LABELS[selected];
}

}  // namespace game
