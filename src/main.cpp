// main.cpp
// ----------------------------------------------------------------------------
// Memory Game — top level: buttons and serial in, game rules, LEDs out.
//
//   * 10 boxes, 5 pairs (1A/1B .. 5A/5B). Game rules live in game_logic.h:
//     first press blue, the other open boxes invite a second press, a found
//     pair stays green until all 5 are found (win animation, clean board), a
//     wrong pair is red for a moment. No press for a minute -> clean board.
//   * On boot: cycle all strips RED → GREEN → BLUE, then all boxes open/white.
//   * Night mode: the Raspberry Pi sends "SLEEP [seconds]" over USB serial at
//     closing time (all strips off, buttons ignored) and "WAKE" at opening
//     time. A missed WAKE is covered by an auto-wake after SLEEP_MAX_S.
// ----------------------------------------------------------------------------

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "states.h"
#include "game_logic.h"
#include "led_control.h"
#include "button_handler.h"

// Hardware task-watchdog timeout. If loop() doesn't feed the watchdog within
// this window, the ESP32 reboots itself — our "no manual intervention for
// 10 years" insurance (NFR2). 5 s is generous: a full red/green/blue POST
// delay cycle is 1 s, and our longest blocking op (LED push of 10 × 180
// pixels) is ~75 ms.
static constexpr uint32_t LOOP_WDT_TIMEOUT_S = 5;

// Wrapper that only prints when the USB-CDC host is actually attached, so a
// detached unit can't block on a full TX buffer. Every line is prefixed with
// the millis() uptime so the Pi-side serial logger can correlate events.
#define LOGF(...)  do { if (Serial) { Serial.printf("[%8lu] ", (unsigned long)millis()); Serial.printf(__VA_ARGS__); } } while (0)
#define LOGLN(s)   do { if (Serial) { Serial.printf("[%8lu] ", (unsigned long)millis()); Serial.println(s); } } while (0)

// Night mode bookkeeping (see enterSleep / exitSleep).
static bool     sleeping       = false;
static uint32_t sleepStartedMs = 0;
static uint32_t sleepMaxMs     = 0;

// Loop health for STATUS: longest gap between loop() runs and LED pushes,
// both measured since the previous STATUS.
static uint32_t lastLoopUs     = 0;
static uint32_t loopMaxUs      = 0;
static uint32_t statsSinceMs   = 0;
static uint32_t statsShowCount = 0;

static void logGame(const char* line) {
    LOGF("%s\n", line);
}

// Push the game's view of every box to the renderer.
static void syncViews() {
    for (uint8_t i = 0; i < NUM_BOXES; ++i) {
        led::setBoxView(i, game::view(i));
    }
}

// ---------------------------------------------------------------------------
// Night mode — SLEEP / WAKE
// ---------------------------------------------------------------------------
static void enterSleep(uint32_t seconds) {
    if (seconds == 0 || seconds > SLEEP_MAX_S_CAP) seconds = SLEEP_MAX_S;

    game::reset(millis(), "sleep");
    led::setSleeping(true);

    sleeping       = true;
    sleepStartedMs = millis();
    sleepMaxMs     = seconds * 1000UL;
    LOGF("[SLEEP] all strips off, auto-wake in %lu s\n", (unsigned long)seconds);
}

static void exitSleep(const char* reason) {
    sleeping = false;
    game::reset(millis(), "wake");
    led::setSleeping(false);   // -> setAllOpen(): every strip back to white 20 %
    LOGF("[WAKE] all boxes idle (%s)\n", reason);
}

static void printStatus() {
    uint32_t now = millis();
    uint32_t remaining = 0;
    if (sleeping) {
        uint32_t elapsed = now - sleepStartedMs;   // wrap-safe
        remaining = (elapsed < sleepMaxMs) ? (sleepMaxMs - elapsed) / 1000UL : 0;
    }
    uint32_t window     = now - statsSinceMs;
    uint32_t pushes     = led::showCount() - statsShowCount;
    uint32_t pushesPerS = window ? (pushes * 1000UL) / window : 0;

    LOGF("[STATUS] sleeping=%d auto_wake_in_s=%lu uptime_s=%lu waiting=%s "
         "style=%s pairs=%u/%u phase=%s loop_max_ms=%lu led_push_per_s=%lu\n",
         sleeping ? 1 : 0,
         (unsigned long)remaining,
         (unsigned long)(now / 1000UL),
         game::selectedLabel(),
         styleName(led::style()),
         game::pairsFound(), NUM_PAIRS,
         game::phaseName(),
         (unsigned long)(loopMaxUs / 1000UL),
         (unsigned long)pushesPerS);

    // Start a new measuring window.
    loopMaxUs      = 0;
    statsSinceMs   = now;
    statsShowCount = led::showCount();
}

static void handleStyle(const char* arg) {
    if (arg && *arg) {
        for (uint8_t i = 0; i < NUM_STYLES; ++i) {
            Style s = static_cast<Style>(i);
            if (strcmp(arg, styleName(s)) == 0) {
                led::setStyle(s);
                LOGF("[STYLE] %s\n", styleName(s));
                return;
            }
        }
        LOGF("[STYLE] unknown '%s'\n", arg);
    }
    LOGF("[STYLE] current=%s options: WHITE YELLOW BLINK\n", styleName(led::style()));
}

// ---------------------------------------------------------------------------
// Serial commands (newline-terminated, case-insensitive):
//   SLEEP [seconds]  night mode: strips off, buttons ignored, auto-wake timer
//   WAKE             leave night mode
//   STATUS           print one status line (also restarts the loop/LED stats)
//   STYLE [name]     invite style WHITE / YELLOW / BLINK (not persisted)
//   RESET            clean board
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
    if (strcmp(line, "STYLE") == 0) {
        handleStyle(arg);
        return;
    }
    if (strcmp(line, "RESET") == 0) {
        if (sleeping) {
            LOGLN(F("[RESET] ignored while sleeping"));
            return;
        }
        game::reset(millis(), "command");
        LOGLN(F("[RESET] board cleared"));
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

    LOGF("[SIM] unknown token '%s' (try SLEEP, WAKE, STATUS, STYLE, RESET, 1A..5B)\n", line);
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

    // FR1: everyone starts open.
    game::begin(millis(), logGame);
    led::setAllOpen();
    LOGLN(F("[IDLE] all boxes white @ 20%"));
    LOGF("[STYLE] %s (default)\n", styleName(led::style()));

#if ENABLE_SERIAL_SIMULATION
    LOGLN(F("[SIM] type 1A..5B + <enter> to simulate button presses"));
#endif
    LOGLN(F("[NIGHT] serial commands: SLEEP [seconds] / WAKE / STATUS"));

    statsSinceMs = millis();

    // Arm the task watchdog on the loop task. If loop() stops feeding it for
    // LOOP_WDT_TIMEOUT_S, the chip reboots — the game recovers by itself.
    esp_task_wdt_init(LOOP_WDT_TIMEOUT_S, /*panic=*/true);
    esp_task_wdt_add(nullptr);   // adds the current (loop) task
}

void loop() {
    esp_task_wdt_reset();   // pet the watchdog every iteration

    uint32_t nowUs = micros();
    if (lastLoopUs != 0 && nowUs - lastLoopUs > loopMaxUs) loopMaxUs = nowUs - lastLoopUs;
    lastLoopUs = nowUs;

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
        led::tick();   // keeps re-sending black so a corrupted frame can't stay lit
        return;
    }

    while (buttons::popPress(&socket)) {
        game::press(socket, millis());
    }

    game::tick(millis());
    syncViews();
    led::tick();
}
