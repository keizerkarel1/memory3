# Memory Game — Agent Context

## What It Is

ESP32-S3 firmware for a 10-box physical memory game with theme "Old & New".
5 pairs of boxes (1A/1B, 2A/2B, ... 5A/5B). Visitors press buttons to find
matching pairs. Visual feedback via SK6812 RGBW LED strips. Infinite loop —
no start, no end, no score.

## Tech Stack

- **C++** with **Arduino framework** on PlatformIO
- **ESP32-S3-WROOM-1 N16R8** (16 MB flash, 8 MB octal PSRAM)
- **Adafruit_NeoPixel** — LED strip control (bit-banged, not DMA)
- **Bounce2** — button debouncing
- **esptool** — flashing from Pi/laptop

## Key Architecture

- Per-box state machine: `IDLE → ACTIVE (blue pulse) → MATCH (green) / NO_MATCH (red) → IDLE`
- Single `waitingSocket` global tracks which box is awaiting a partner
- Task watchdog (5 s) — auto-reboots on hang
- Serial simulation mode (`ENABLE_SERIAL_SIMULATION 1`) — type "1A", "1B" etc. to test without hardware
- Power-on self-test: cycles all strips RED → GREEN → BLUE

## Hardware Notes

- **GPIO 19, 20** are reserved for native USB (D-/D+). Do not use.
- **GPIO 33–37** are reserved for octal PSRAM. Do not use.
- **GPIO 26–32** reserved for internal SPI flash.
- Level shifters (TXS0108E) required between ESP32 3.3V and LED strip 5V data.
- Power topology: Mean Well 5V/30A PSU feeds ESP32 `5V` pin + all LED strips.
  Pi USB serial cable can also power the ESP32 (diode-OR safe).
- 2 USB-C ports: **UART** (CH340, `/dev/ttyUSB*`) for flashing; **USB** (native, `/dev/ttyACM*`) for JTAG.

## Related Docs

- `design.md` — full design document with requirements, topology, and rationale
- `README.md` — hardware specs, GPIO mapping, build/flash instructions, tuning knobs