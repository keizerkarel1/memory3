// button_handler.cpp
// ----------------------------------------------------------------------------
// Debounced buttons using Bounce2 at BUTTON_DEBOUNCE_MS (50 ms per spec).
// Events are queued into a small ring buffer so that two near-simultaneous
// presses are both delivered to the state machine.
// ----------------------------------------------------------------------------
#include "button_handler.h"

#include <Bounce2.h>

namespace buttons {

namespace {

Bounce debouncers[NUM_BOXES];

// Small ring buffer of press events.
constexpr uint8_t QUEUE_SIZE = 16;   // more than enough for 10 buttons
uint8_t queue[QUEUE_SIZE];
volatile uint8_t qHead = 0;          // read position
volatile uint8_t qTail = 0;          // write position

void enqueue(uint8_t socket) {
    uint8_t next = (qTail + 1) % QUEUE_SIZE;
    if (next == qHead) {
        // Full — drop oldest so the newest press is preserved.
        qHead = (qHead + 1) % QUEUE_SIZE;
    }
    queue[qTail] = socket;
    qTail = next;
}

}  // namespace

void begin() {
    for (uint8_t i = 0; i < NUM_BOXES; ++i) {
        pinMode(BUTTON_PINS[i], INPUT_PULLUP);
        debouncers[i].attach(BUTTON_PINS[i]);
        debouncers[i].interval(BUTTON_DEBOUNCE_MS);
    }
    qHead = qTail = 0;
}

void tick() {
    for (uint8_t i = 0; i < NUM_BOXES; ++i) {
        debouncers[i].update();
        // Active-low: a "press" is a falling edge.
        if (debouncers[i].fell()) {
            enqueue(i);
        }
    }
}

bool popPress(uint8_t* socket) {
    if (qHead == qTail) return false;
    *socket = queue[qHead];
    qHead = (qHead + 1) % QUEUE_SIZE;
    return true;
}

void injectPress(uint8_t socket) {
    if (socket >= NUM_BOXES) return;
    enqueue(socket);
}

}  // namespace buttons