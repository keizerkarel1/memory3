// main.cpp
// ----------------------------------------------------------------------------
// Memory Game — top-level state machine.
//
// Memory Game — game rules (summarised from design.md):
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
//   * Night mode: the Raspberry Pi sends "SLEEP [seconds]" over USB serial at
//     closing time (all strips off, buttons ignored) and "WAKE" at opening
//     time. A missed WAKE is covered by an auto-wake after SLEEP_MAX_S.
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
// detached unit can't block on a full TX buffer. Every line is prefixed with
// the millis() uptime so the Pi-side serial logger can correlate events.
#define LOGF(...)  do { if (Serial) { Serial.printf("[%8lu] ", (unsigned long)millis()); Serial.printf(__VA_ARGS__); } } while (0)
#define LOGLN(s)   do { if (Serial) { Serial.printf("[%8lu] ", (unsigned long)millis()); Serial.println(s); } } while (0)

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

// Night mode bookkeeping (see enterSleep / exitSleep).
static bool     sleeping       = false;
static uint32_t sleepStartedMs = 0;
static uint32_t sleepMaxMs     = 0;

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
// Night mode — SLEEP / WAKE
// ---------------------------------------------------------------------------
static void enterSleep(uint32_t seconds) {
    if (seconds == 0 || seconds > SLEEP_MAX_S_CAP) seconds = SLEEP_MAX_S;

    waitingSocket = 0xFF;
    for (uint8_t i = 0; i < NUM_BOXES; ++i) {
        box[i].state       = BoxState::IDLE;
        box[i].enteredAtMs = millis();
    }
    led::setSleeping(true);

    sleeping       = true;
    sleepStartedMs = millis();
    sleepMaxMs     = seconds * 1000UL;
    LOGF("[SLEEP] all strips off, auto-wake in %lu s\n", (unsigned long)seconds);
}

static void exitSleep(const char* reason) {
    sleeping = false;
    led::setSleeping(false);   // -> setAllIdle(): every strip back to white 20 %
    for (uint8_t i = 0; i < NUM_BOXES; ++i) {
        box[i].state       = BoxState::IDLE;
        box[i].enteredAtMs = millis();
    }
    waitingSocket = 0xFF;
    LOGF("[WAKE] all boxes idle (%s)\n", reason);
}

static void printStatus() {
    uint32_t remaining = 0;
    if (sleeping) {
        uint32_t elapsed = millis() - sleepStartedMs;   // wrap-safe
        remaining = (elapsed < sleepMaxMs) ? (sleepMaxMs - elapsed) / 1000UL : 0;
    }
    LOGF("[STATUS] sleeping=%d auto_wake_in_s=%lu uptime_s=%lu waiting=%s\n",
         sleeping ? 1 : 0,
         (unsigned long)remaining,
         (unsigned long)(millis() / 1000UL),
         waitingSocket == 0xFF ? "none" : SOCKET_LABELS[waitingSocket]);
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
// Serial commands (newline-terminated, case-insensitive):
//   SLEEP [seconds]  night mode: strips off, buttons ignored, auto-wake timer
//   WAKE             leave night mode
//   STATUS           print one status line
//   1A .. 5B         simulate a button press (only if ENABLE_SERIAL_SIMULATION)
// ---------------------------------------------------------------------------
static void handleSerialCommand(char* line) {
    // Upper-case in place for case-insensitive matching.
    for (char* c = line; *c; ++c) {
        if (*c >= 'a' && *c <= 'z') *c -= 32;
    }

    // Split "CMD ARG" on the first space.
    char* arg = strchr(line, ' ');
    if (arg) {
        *arg++ = '\0';
        while (*arg == ' ') ++arg;
    }

    if (strcmp(line, "SLEEP") == 0) {
        uint32_t seconds = SLEEP_MAX_S;
        if (arg && *arg) {
            unsigned long parsed = strtoul(arg, nullptr, 10);
            seconds = (parsed == 0) ? SLEEP_MAX_S : (uint32_t)parsed;
        }
        enterSleep(seconds);
        return;
    }
    if (strcmp(line, "WAKE") == 0) {
        if (sleeping) exitSleep("command");
        else          LOGLN(F("[WAKE] already awake"));
        return;
    }
    if (strcmp(line, "STATUS") == 0) {
        printStatus();
        return;
    }

#if ENABLE_SERIAL_SIMULATION
    for (uint8_t i = 0; i < NUM_BOXES; ++i) {
        if (strcmp(line, SOCKET_LABELS[i]) == 0) {
            LOGF("[SIM] press %s\n", SOCKET_LABELS[i]);
            buttons::injectPress(i);
            return;
        }
    }
#endif

    LOGF("[SIM] unknown token '%s' (try SLEEP, WAKE, STATUS, 1A..5B)\n", line);
}

static void pollSerialCommands() {
    static char    buf[SERIAL_CMD_BUF];
    static uint8_t len = 0;

    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r' || c == '\n') {
            if (len == 0) continue;
            buf[len] = '\0';
            handleSerialCommand(buf);
            len = 0;
        } else if (len < sizeof(buf) - 1) {
            buf[len++] = c;
        } else {
            // Overflow — discard the line.
            len = 0;
        }
    }
}

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
    LOGLN(F("[NIGHT] serial commands: SLEEP [seconds] / WAKE / STATUS"));

    // Arm the task watchdog on the loop task. If loop() stops feeding it for
    // LOOP_WDT_TIMEOUT_S, the chip reboots — the game recovers by itself.
    esp_task_wdt_init(LOOP_WDT_TIMEOUT_S, /*panic=*/true);
    esp_task_wdt_add(nullptr);   // adds the current (loop) task
}

void loop() {
    esp_task_wdt_reset();   // pet the watchdog every iteration

    buttons::tick();
    pollSerialCommands();

    uint8_t socket;
    if (sleeping) {
        // Night mode: swallow presses, keep strips dark, watch the auto-wake
        // timer (unsigned subtraction is safe across the millis() wrap).
        while (buttons::popPress(&socket)) { /* ignored */ }
        if (millis() - sleepStartedMs >= sleepMaxMs) {
            exitSleep("timeout");
        }
        return;
    }

    while (buttons::popPress(&socket)) {
        handlePress(socket);
    }

    sweepTimeouts();
    led::tick();
}
