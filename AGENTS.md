# Memory Game — Agent Context

## What It Is

ESP32-S3 firmware for a 10-box physical memory game with theme "Old & New".
5 pairs of boxes (1A/1B, 2A/2B, ... 5A/5B). Visitors press buttons to find
matching pairs. Visual feedback via SK6812 RGBW LED strips. Found pairs stay
green; all 5 found = win animation and a clean board (since 2026-10).

## Tech Stack

- **C++** with **Arduino framework** on PlatformIO
- **ESP32-S3-WROOM-1 N16R8** (16 MB flash, 8 MB octal PSRAM)
- **Adafruit_NeoPixel** — LED strip control (RMT, one strip per `show()`; static colours re-sent ~1x/s to heal corrupted frames)
- **Bounce2** — button debouncing
- **esptool** — flashing from Pi/laptop

## Key Architecture

- Game rules in `src/game_logic.{h,cpp}` — no Arduino includes, time passed in; host unit tests in
  `test/host/`, run with `python tools/run_host_tests.py` (needs g++/clang++ or `pip install ziglang`)
- Box views (`states.h`): `OPEN → SELECTED (solid blue, others INVITE) → CHECKING (blue blink) → MATCH (green, locked) /
  NO_MATCH (red 2.5 s)`; all 5 pairs → `WIN` animation → clean board; 15 s selection timeout, 60 s idle reset
- `led_control` turns views into colours and only pushes strips whose colour changed; invite style WHITE / YELLOW /
  BLINK switchable with serial `STYLE` (not persisted; default per build env, see `platformio.ini`)
- `config.h` is included by the host tests: keep it free of Arduino APIs
- Task watchdog (5 s) — auto-reboots on hang
- Serial simulation mode (`ENABLE_SERIAL_SIMULATION 1`) — type "1A", "1B" etc. to test without hardware
- Night mode: serial `SLEEP [seconds]` / `WAKE` / `STATUS` from the Pi (`vonk-ops` repo); strips off, presses ignored, auto-wake after `SLEEP_MAX_S`; sleep clears the game
- Log tags used for play statistics: `[GAME] START/PAIR/WON/RESET/READY`, `[SELECT]`, `[CHECK]`, `[CANCEL]`, `[IGNORE]`
- Power-on self-test: cycles all strips RED → GREEN → BLUE

## Hardware Notes

- **GPIO 19, 20** are reserved for native USB (D-/D+). Do not use.
- **GPIO 33–37** are reserved for octal PSRAM. Do not use.
- **GPIO 26–32** reserved for internal SPI flash.
- Level shifters (TXS0108E) required between ESP32 3.3V and LED strip 5V data.
- Power topology: Mean Well 5V/30A PSU feeds ESP32 `5V` pin + all LED strips.
  Pi USB serial cable can also power the ESP32 (diode-OR safe).
- 2 USB-C ports: **UART** (CH340, `/dev/ttyUSB*`) and **USB** (native, `/dev/ttyACM*`). **Deployed: native USB → `/dev/ttyACM0` on the Pi**, held open by `serial_logger.py`.
- `platform = espressif32 @ 6.13.0` is pinned on purpose (Arduino core 2.x WDT API).

## Related Docs

- `design.md` — full design document with requirements, topology, and rationale
- `README.md` — hardware specs, GPIO mapping, build/flash instructions, tuning knobs