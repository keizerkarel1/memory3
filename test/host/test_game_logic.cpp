// test_game_logic.cpp
// ----------------------------------------------------------------------------
// Host unit tests for src/game_logic.cpp. Plain C++17, no framework:
//   python tools/run_host_tests.py
// Every scenario runs twice: from t = 1000 ms and from just before the 32-bit
// millis() wrap, so all timers are also tested across the wrap.
// ----------------------------------------------------------------------------
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game_logic.h"

namespace {

int checks   = 0;
int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++checks;                                                            \
        if (!(cond)) {                                                       \
            ++failures;                                                      \
            std::printf("  FAIL %s:%d (base %lu): %s\n", __FILE__, __LINE__, \
                        (unsigned long)base, #cond);                         \
        }                                                                    \
    } while (0)

std::vector<std::string> logs;
void capture(const char* line) { logs.push_back(line); }

bool logged(const char* needle) {
    for (const auto& l : logs) {
        if (l.find(needle) != std::string::npos) return true;
    }
    return false;
}

uint32_t base = 0;   // start time of the current scenario
uint32_t now  = 0;

// Socket indices (see config.h): 1A=0 1B=1 2A=2 2B=3 ... 5A=8 5B=9.
constexpr uint8_t S1A = 0, S1B = 1, S2A = 2, S2B = 3, S3A = 4;

void start() {
    logs.clear();
    now = base;
    game::begin(now, capture);
}

// Advance the clock like loop() would (tick every 10 ms).
void advance(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 10) {
        now += 10;
        game::tick(now);
    }
}

void press(uint8_t socket) { game::press(socket, now); game::tick(now); }

BoxView v(uint8_t socket) { return game::view(socket); }

int countView(BoxView want) {
    int n = 0;
    for (uint8_t i = 0; i < NUM_BOXES; ++i) n += (v(i) == want);
    return n;
}

void findPair(uint8_t a) {
    press(a);
    press(partnerOf(a));
    advance(CHECK_MS);
}

// ---------------------------------------------------------------------------

void testCleanBoard() {
    start();
    CHECK(countView(BoxView::OPEN) == NUM_BOXES);
    CHECK(game::pairsFound() == 0);
    CHECK(std::strcmp(game::selectedLabel(), "none") == 0);
    advance(GAME_IDLE_RESET_MS * 2);
    CHECK(logs.empty());   // nothing happens without presses
}

void testFirstPressInvitesOthers() {
    start();
    press(S1A);
    CHECK(v(S1A) == BoxView::SELECTED);
    CHECK(countView(BoxView::INVITE) == NUM_BOXES - 1);
    CHECK(logged("[GAME] START"));
    CHECK(logged("[SELECT] 1A"));
    CHECK(std::strcmp(game::selectedLabel(), "1A") == 0);
}

void testRepressSelectedIgnored() {
    start();
    press(S1A);
    press(S1A);
    CHECK(v(S1A) == BoxView::SELECTED);
    CHECK(logged("[IGNORE] 1A (already waiting)"));
}

void testWrongPair() {
    start();
    press(S1A);
    press(S2A);
    CHECK(v(S1A) == BoxView::CHECKING);
    CHECK(v(S2A) == BoxView::CHECKING);
    CHECK(countView(BoxView::OPEN) == NUM_BOXES - 2);   // invite stopped
    CHECK(logged("[CHECK] 1A + 2A -> NO_MATCH"));

    advance(CHECK_MS - 10);
    CHECK(v(S1A) == BoxView::CHECKING);
    advance(10);
    CHECK(v(S1A) == BoxView::NO_MATCH);
    CHECK(v(S2A) == BoxView::NO_MATCH);
    CHECK(game::pairsFound() == 0);

    advance(NO_MATCH_MS - 10);
    CHECK(v(S1A) == BoxView::NO_MATCH);
    advance(10);
    CHECK(v(S1A) == BoxView::OPEN);
    CHECK(v(S2A) == BoxView::OPEN);
}

void testCorrectPairLocks() {
    start();
    findPair(S1A);
    CHECK(v(S1A) == BoxView::MATCH);
    CHECK(v(S1B) == BoxView::MATCH);
    CHECK(game::pairsFound() == 1);
    CHECK(logged("[GAME] PAIR 1/5"));

    press(S1A);
    CHECK(v(S1A) == BoxView::MATCH);
    CHECK(logged("[IGNORE] 1A (in MATCH)"));
    CHECK(std::strcmp(game::selectedLabel(), "none") == 0);

    // Green stays as long as someone keeps playing (press every ~59 s).
    for (int i = 0; i < 5; ++i) {
        press(S2A);
        advance(SELECT_TIMEOUT_MS);
        advance(GAME_IDLE_RESET_MS - SELECT_TIMEOUT_MS - 1000);
    }
    CHECK(v(S1A) == BoxView::MATCH);
}

void testPressesDuringCheckIgnored() {
    start();
    press(S1A);
    press(S2A);
    press(S3A);
    CHECK(v(S3A) == BoxView::OPEN);
    CHECK(logged("[IGNORE] 3A (checking)"));
    advance(CHECK_MS);
    CHECK(v(S3A) == BoxView::OPEN);
    CHECK(std::strcmp(game::selectedLabel(), "none") == 0);
}

void testNewSelectionWhileRed() {
    start();
    press(S1A);
    press(S2A);
    advance(CHECK_MS);
    press(S1A);                                   // red box
    CHECK(logged("[IGNORE] 1A (in NO_MATCH)"));
    press(S3A);                                   // open box -> new selection
    CHECK(v(S3A) == BoxView::SELECTED);
    CHECK(v(S1A) == BoxView::NO_MATCH);           // red boxes don't invite
    CHECK(v(S2A) == BoxView::NO_MATCH);
    CHECK(countView(BoxView::INVITE) == NUM_BOXES - 3);
    advance(NO_MATCH_MS);
    CHECK(v(S1A) == BoxView::INVITE);             // open again -> joins the invite
}

void testSelectionTimeout() {
    start();
    findPair(S1A);
    press(S2A);
    advance(SELECT_TIMEOUT_MS - 10);
    CHECK(v(S2A) == BoxView::SELECTED);
    advance(10);
    CHECK(v(S2A) == BoxView::OPEN);
    CHECK(countView(BoxView::INVITE) == 0);
    CHECK(v(S1A) == BoxView::MATCH);
    CHECK(logged("[CANCEL] 2A timeout"));
}

void testIdleReset() {
    start();
    findPair(S1A);
    findPair(S2A);
    advance(GAME_IDLE_RESET_MS - CHECK_MS - 10);
    CHECK(v(S1A) == BoxView::MATCH);
    advance(10);
    CHECK(countView(BoxView::OPEN) == NUM_BOXES);
    CHECK(game::pairsFound() == 0);
    CHECK(logged("[GAME] RESET idle pairs=2/5 attempts=2"));

    // The next press starts a new game.
    logs.clear();
    press(S3A);
    CHECK(logged("[GAME] START"));
}

void testIgnoredPressKeepsGameAlive() {
    start();
    findPair(S1A);
    advance(GAME_IDLE_RESET_MS - 5000);
    press(S1A);                                   // ignored, but someone is there
    advance(GAME_IDLE_RESET_MS - 10);
    CHECK(v(S1A) == BoxView::MATCH);
    advance(10);
    CHECK(v(S1A) == BoxView::OPEN);
}

void testFullGameAndWin() {
    start();
    press(S1A);                                   // one wrong attempt first
    press(S2B);
    advance(CHECK_MS + NO_MATCH_MS);
    for (uint8_t a = 0; a < NUM_BOXES; a += 2) findPair(a);

    CHECK(game::pairsFound() == NUM_PAIRS);
    CHECK(logged("[GAME] WON"));
    CHECK(logged("attempts=6"));
    CHECK(std::strcmp(game::phaseName(), "WIN_HOLD") == 0);
    CHECK(countView(BoxView::MATCH) == NUM_BOXES);

    press(S1A);
    CHECK(logged("[IGNORE] 1A (win)"));

    advance(WIN_HOLD_MS);
    CHECK(std::strcmp(game::phaseName(), "WIN_ANIM") == 0);
    CHECK(countView(BoxView::WIN) == NUM_BOXES);
    press(S2A);
    CHECK(countView(BoxView::WIN) == NUM_BOXES);

    advance(WIN_ANIM_MS);
    CHECK(countView(BoxView::OPEN) == NUM_BOXES);
    CHECK(game::pairsFound() == 0);
    CHECK(std::strcmp(game::phaseName(), "PLAY") == 0);
    CHECK(logged("[GAME] READY"));

    logs.clear();
    press(S1A);
    CHECK(v(S1A) == BoxView::SELECTED);
    CHECK(logged("[GAME] START"));
}

void testResetMidGameAndMidWin() {
    start();
    findPair(S1A);
    press(S2A);
    game::reset(now, "sleep");
    CHECK(countView(BoxView::OPEN) == NUM_BOXES);
    CHECK(logged("[GAME] RESET sleep pairs=1/5 attempts=1"));

    start();
    for (uint8_t a = 0; a < NUM_BOXES; a += 2) findPair(a);
    advance(WIN_HOLD_MS + 100);
    game::reset(now, "sleep");
    CHECK(countView(BoxView::OPEN) == NUM_BOXES);
    CHECK(std::strcmp(game::phaseName(), "PLAY") == 0);

    // Reset on a clean board stays silent.
    start();
    game::reset(now, "wake");
    CHECK(logs.empty());
}

void testCheckBlinkInterruptedByReset() {
    start();
    press(S1A);
    press(S1B);
    game::reset(now, "command");
    advance(CHECK_MS * 2);
    CHECK(countView(BoxView::OPEN) == NUM_BOXES);
    CHECK(game::pairsFound() == 0);
}

void testOutOfRangeSocket() {
    start();
    game::press(NUM_BOXES, now);
    game::press(0xFF, now);
    CHECK(logs.empty());
    CHECK(game::view(NUM_BOXES) == BoxView::OPEN);
}

}  // namespace

int main() {
    const uint32_t bases[] = {1000UL, 0xFFFFFFFFUL - 30000UL};
    for (uint32_t b : bases) {
        base = b;
        testCleanBoard();
        testFirstPressInvitesOthers();
        testRepressSelectedIgnored();
        testWrongPair();
        testCorrectPairLocks();
        testPressesDuringCheckIgnored();
        testNewSelectionWhileRed();
        testSelectionTimeout();
        testIdleReset();
        testIgnoredPressKeepsGameAlive();
        testFullGameAndWin();
        testResetMidGameAndMidWin();
        testCheckBlinkInterruptedByReset();
        testOutOfRangeSocket();
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
