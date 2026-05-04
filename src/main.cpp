// main.cpp
// ----------------------------------------------------------------------------
// Memory Game — top-level state machine.
//
// Game rules (summarised from memory-opus.md):
//   * 10 boxes, 5 pairs (1A/1B .. 5A/5B).
//   * On boot: cycle all strips RED → GREEN → BLUE, then go IDLE.
//   * Pressing a button on an IDLE box → that box becomes ACTIVE (blue pulse).
//   * Pressing a second button:
//       - If the two ACTIVE boxes form a pair, both go MATCH (green).
//       - Otherwise both go NO_MATCH (red).
//     Any other boxes that happened to be ACTIVE at that moment time out
//     independently.
//   * All non-idle boxes revert to IDLE after STATE_TIMEOUT_MS (10 s).
//   * No start, no end, no score, no sound. Infinite loop.
//
// Design note: the state machine runs *per box*. A small global holds "which
// box is currently waiting for a partner" so that the second press can be
// paired up. If a third press arrives before a match check, we treat it as
// a new "first press" (the existing waiting box keeps pulsing and will time
// out on its own).
// ----------------------------------------------------------------------------

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "states.h"
#include "led_control.h"
#include "button_handler.h"

// Hardware task-watchdog timeout. If loop() doesn't feed the watchdog within
// this window, the ESP32 reboots itself — our "no manual intervention for
// 10 years" insurance (NFR2). 5 s is generous: a full red/green/blue POST
// delay cycle is 1 s, and our longest blocking op (LED push of 10 × 180
// pixels, bit-banged) is ~55 ms.
static constexpr uint32_t LOOP_WDT_TIMEOUT_S = 5;

// Wrapper that only prints when the USB-CDC host is actually attached, so a
// detached unit can't block on a full TX buffer.
#define LOGF(...)  do { if (Serial) Serial.printf(__VA_ARGS__); } while (0)
#define LOGLN(s)   do { if (Serial) Serial.println(s); } while (0)

// ---------------------------------------------------------------------------
// Per-box runtime state.
// ---------------------------------------------------------------------------
struct BoxRuntime {
    BoxState  state;
    uint32_t  enteredAtMs;   // millis() when the current state was entered
};

static BoxRuntime box[NUM_BOXES];

// Index of a box currently ACTIVE and waiting to be paired with a second
// press, or 0xFF if none.
static uint8_t waitingSocket = 0xFF;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void setState(uint8_t socket, BoxState s) {
    box[socket].state       = s;
    box[socket].enteredAtMs = millis();
    led::setBoxState(socket, s);
    LOGF("[%s] %s\n", stateName(s), SOCKET_LABELS[socket]);
}

static void resetToIdle(uint8_t socket) {
    setState(socket, BoxState::IDLE);
    if (waitingSocket == socket) {
        waitingSocket = 0xFF;
    }
}

// ---------------------------------------------------------------------------
// Press handler — the core game logic
// ---------------------------------------------------------------------------
static void handlePress(uint8_t socket) {
    BoxState s = box[socket].state;

    // Ignore presses on boxes already showing a result; they'll auto-reset.
    if (s == BoxState::MATCH || s == BoxState::NO_MATCH) {
        LOGF("[IGNORE] %s (in %s)\n", SOCKET_LABELS[socket], stateName(s));
        return;
    }

    // Pressing the already-waiting box again — treat as no-op to avoid
    // ambiguous "self-match". Keep it pulsing.
    if (s == BoxState::ACTIVE && waitingSocket == socket) {
        LOGF("[IGNORE] %s (already waiting)\n", SOCKET_LABELS[socket]);
        return;
    }

    // First press (no one waiting) → this box becomes ACTIVE and is the
    // candidate for the next match check.
    if (waitingSocket == 0xFF) {
        setState(socket, BoxState::ACTIVE);
        waitingSocket = socket;
        return;
    }

    // Second press → run the match check between 'waitingSocket' and 'socket'.
    uint8_t a = waitingSocket;
    uint8_t b = socket;
    waitingSocket = 0xFF;

    // Make sure the new box is also recorded as ACTIVE (briefly) for logging
    // consistency — we go straight to the result state.
    bool matched = (partnerOf(a) == b);
    BoxState result = matched ? BoxState::MATCH : BoxState::NO_MATCH;

    LOGF("[CHECK] %s + %s -> %s\n",
         SOCKET_LABELS[a], SOCKET_LABELS[b], stateName(result));

    setState(a, result);
    setState(b, result);
}

// ---------------------------------------------------------------------------
// Timeout sweep — any box in a non-idle state for > STATE_TIMEOUT_MS reverts.
// ---------------------------------------------------------------------------
static void sweepTimeouts() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < NUM_BOXES; ++i) {
        if (box[i].state == BoxState::IDLE) continue;
        if (now - box[i].enteredAtMs >= STATE_TIMEOUT_MS) {
            LOGF("[TIMEOUT] %s -> IDLE\n", SOCKET_LABELS[i]);
            resetToIdle(i);
        }
    }
}

// ---------------------------------------------------------------------------
// Optional serial-simulation: type "1A<enter>" etc. to simulate a button.
// ---------------------------------------------------------------------------
#if ENABLE_SERIAL_SIMULATION
static void pollSerialSimulation() {
    static char   buf[8];
    static uint8_t len = 0;

    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r' || c == '\n') {
            if (len == 0) continue;
            buf[len] = '\0';

            // Upper-case for case-insensitive match.
            for (uint8_t i = 0; i < len; ++i) {
                if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] -= 32;
            }

            int8_t matched = -1;
            for (uint8_t i = 0; i < NUM_BOXES; ++i) {
                if (strcmp(buf, SOCKET_LABELS[i]) == 0) {
                    matched = i;
                    break;
                }
            }
            if (matched >= 0) {
                LOGF("[SIM] press %s\n", SOCKET_LABELS[matched]);
                buttons::injectPress(matched);
            } else {
                LOGF("[SIM] unknown token '%s' (try 1A..5B)\n", buf);
            }
            len = 0;
        } else if (len < sizeof(buf) - 1) {
            buf[len++] = c;
        } else {
            // Overflow — reset.
            len = 0;
        }
    }
}
#endif

// ---------------------------------------------------------------------------
// setup() / loop()
// ---------------------------------------------------------------------------
void setup() {
    // Start Serial immediately but do NOT wait for a host — in a deployed unit
    // the USB link may be unused or only observed by the RPi later.
    Serial.begin(SERIAL_BAUD);

    LOGLN("");
    LOGLN(F("==============================================="));
    LOGLN(F("  Memory Game — Vonk Gespreksstarters"));
    LOGLN(F("  ESP32-S3-WROOM-1 N16R8"));
    LOGLN(F("==============================================="));

    led::begin();
    buttons::begin();

    // FR8: boot-time self-test.
    led::powerOnTest();

    // FR1: everyone starts IDLE.
    for (uint8_t i = 0; i < NUM_BOXES; ++i) {
        box[i].state       = BoxState::IDLE;
        box[i].enteredAtMs = millis();
    }
    led::setAllIdle();
    LOGLN(F("[IDLE] all boxes white @ 20%"));

#if ENABLE_SERIAL_SIMULATION
    LOGLN(F("[SIM] type 1A..5B + <enter> to simulate button presses"));
#endif

    // Arm the task watchdog on the loop task. If loop() stops feeding it for
    // LOOP_WDT_TIMEOUT_S, the chip reboots — the game recovers by itself.
    esp_task_wdt_init(LOOP_WDT_TIMEOUT_S, /*panic=*/true);
    esp_task_wdt_add(nullptr);   // adds the current (loop) task
}

void loop() {
    esp_task_wdt_reset();   // pet the watchdog every iteration

    buttons::tick();

#if ENABLE_SERIAL_SIMULATION
    pollSerialSimulation();
#endif

    uint8_t socket;
    while (buttons::popPress(&socket)) {
        handlePress(socket);
    }

    sweepTimeouts();
    led::tick();
}
