# Memory Game — Vonk Gespreksstarters

ESP32-S3 firmware for the 10-box "Old & New" memory game.
See [`memory-opus.md`](memory-opus.md) for the full design document.

---

## Hardware

| Item | Value |
|---|---|
| MCU module | ESP32-S3-WROOM-1 **N16R8** (16 MB flash, 8 MB **octal** PSRAM) |
| Dev board | ESP32-S3-DevKitC-1 v1.0 |
| LED strips | 10 × SK6812 RGBW (72–180 LEDs each) |
| Buttons | 10 × NO vandalproof pushbutton, to GND |
| Level shift | 2 × TXS0108E between ESP32 3.3 V and LED 5 V data |
| PSU | Mean Well 5 V / 30 A |

### GPIO mapping

The design document originally proposed GPIO 1–10 for buttons and GPIO 11–20 for
LED data. That mapping is **not usable on this module**, so we remapped:

* GPIO 19, 20 — reserved for native USB-CDC-JTAG (D−/D+). Using them would kill
  programming and serial.
* GPIO 33–37 — reserved for the **octal PSRAM** on N16R8. Physically exposed on
  the header but hard-wired to the PSRAM die.
* GPIO 26–32 — reserved for internal SPI flash.
* GPIO 43, 44 — U0TXD / U0RXD (UART port via CH340).

Final mapping (see `src/config.h`):

| Socket | Button GPIO | LED data GPIO |
|--------|-------------|---------------|
| 1A     | 4           | 1             |
| 1B     | 5           | 2             |
| 2A     | 7           | 42            |
| 2B     | 15          | 41            |
| 3A     | 16          | 40            |
| 3B     | 17          | 39            |
| 4A     | 18          | 38            |
| 4B     | 8           | 21            |
| 5A     | 3           | 47            |
| 5B     | 46          | 48 *(also drives on-board RGB LED — cosmetic)* |

Buttons use `INPUT_PULLUP`; wire the other side of each button to **GND**.
LED data lines go through the TXS0108E to the strip `DIN`.

---

## Build / flash (local laptop)

```bash
# If not already on the machine:
pip install --user platformio          # or use VS Code PlatformIO extension

# From project root:
pio run                                # build
pio run -t upload --upload-port /dev/ttyACM0   # flash
pio device monitor                     # serial monitor (see quirks below)
```

### Linux permission note
If upload fails with `Permission denied: '/dev/ttyACM0'`, add yourself to the
`dialout` group once and log back in:

```bash
sudo usermod -aG dialout $USER
```

### Which USB port to use

The ESP32-S3-DevKitC-1 has **two USB-C ports**:

| Port label | Behind the scenes | Linux device | Recommended for |
|---|---|---|---|
| **UART** | CH340 USB-UART bridge wired to ESP32 U0TXD / U0RXD | `/dev/ttyUSB*` | **Flashing, monitoring, museum deployment** |
| **USB** | ESP32-S3's built-in native USB-CDC-JTAG | `/dev/ttyACM*` | Only if you need JTAG / USB-HID features |

Use the **UART port** unless you have a specific reason not to. The CH340
bridge handles DTR/RTS correctly, giving you automatic bootloader entry on
flash and a serial monitor that **stays connected across resets and reflashes**.

If you use the **USB** (native) port, be aware of these quirks:
1. First flash requires a manual bootloader entry: hold `BOOT`, tap `RESET`,
   release `BOOT`. Subsequent flashes auto-reset.
2. The serial monitor drops every time the board resets or is reflashed,
   because the CDC endpoint disappears with the chip. Re-open it after each
   reset.
3. Do **not** toggle DTR/RTS manually — on this port those are bootloader
   mode selectors, not a soft-reset signal.

---

## Serial simulation (development without hardware)

With no buttons/LEDs wired, you can still validate the full state machine over
serial. Open the monitor at 115200 baud and type a socket label + Enter:

```
1A <Enter>     # first half of pair 1 → ACTIVE (blue pulse)
1B <Enter>     # matching second half → MATCH (green)
2A <Enter>
3B <Enter>     # non-matching → NO_MATCH (red)
```

Valid tokens: `1A 1B 2A 2B 3A 3B 4A 4B 5A 5B` (case-insensitive).
To disable this in production, set `ENABLE_SERIAL_SIMULATION 0` in
`src/config.h`.

---

## File layout

```
memory/
├── platformio.ini              # board, framework, libs, flash/PSRAM config
├── memory-opus.md              # full design document
├── README.md                   # this file
└── src/
    ├── main.cpp                # setup/loop + per-box state machine
    ├── config.h                # GPIO map, timings, brightness, pair logic
    ├── states.h                # BoxState enum
    ├── led_control.{h,cpp}     # Adafruit_NeoPixel rendering + pulse anim
    └── button_handler.{h,cpp}  # Bounce2 + press event queue
```

### Why Adafruit_NeoPixel and not NeoPixelBus
The design doc calls out NeoPixelBus. On ESP32-S3 the RMT peripheral only has
**4 TX channels**, so 10 independent DMA-driven strips is not possible without
parallel-I2S tricks that complicate maintenance. `Adafruit_NeoPixel` bit-bangs
each strip; the ~5 ms blocking `show()` per 180-LED strip is irrelevant because
we only update LEDs on state changes plus a slow (~50 Hz) pulse refresh.

### Robustness features
* **Task watchdog** — `esp_task_wdt` is armed on the `loop()` task with a 5 s
  timeout. If the main loop is ever stuck (bad ISR, library bug, cosmic ray),
  the ESP32 reboots itself; the game re-runs POST and returns to idle with no
  human intervention (NFR2).
* **No host-wait at boot** — `setup()` does not block waiting for a serial
  host, so a deployed unit with no USB companion starts the game immediately.
* **Guarded serial prints** — all logging goes through `LOGF`/`LOGLN` macros
  that skip the call when no USB-CDC host is attached, so a full TX buffer
  can't stall the game loop.
* **RGBW white idle** — idle uses only the dedicated white LED of the SK6812,
  not R+G+B combined, roughly halving current draw at 20 % brightness (NFR1).

---

## Remote maintenance (museum / RPi5)

The firmware needs **no special configuration** for remote use — it just
exposes itself as a serial device. Everything is done from the RPi side.

### One-time RPi5 setup

```bash
sudo apt install python3-pip python3-venv
pipx install platformio              # or: pip install --user platformio
sudo usermod -aG dialout $USER
sudo reboot                          # so the group change takes effect
```

Make sure SSH / Tailscale is running so you can reach the Pi remotely (NFR3).

### Flashing and monitoring from the Pi

Assuming the technician connected the ESP32's **UART port** to the RPi5 (see
"Which USB port to use" above), the device will appear as `/dev/ttyUSB0`:

```bash
ssh pi@rpi5-museum.local
# Live serial logs — stays connected across ESP32 resets:
pio device monitor -p /dev/ttyUSB0 -b 115200

# Flash a new build produced on the Pi itself:
cd ~/memory && git pull            # or rsync from the laptop
pio run -t upload --upload-port /dev/ttyUSB0
```

If you build on your laptop and only want to push the compiled `firmware.bin`:

```bash
# On the laptop:
scp .pio/build/esp32-s3-devkitc-1/firmware.bin pi@rpi5-museum.local:/tmp/

# On the RPi, via SSH:
esptool.py --port /dev/ttyUSB0 write_flash 0x10000 /tmp/firmware.bin
```
(`0x10000` is the app partition offset for the default 16 MB partition table.)

> If the technician used the **USB** (native) port instead, swap every
> `/dev/ttyUSB0` above for `/dev/ttyACM0`, and remember the quirks in the
> "Which USB port to use" section (manual BOOT+RESET for first flash,
> monitor drops across resets).

---

## Tuning knobs (all in `src/config.h`)

| Constant | Purpose |
|---|---|
| `LEDS_PER_STRIP` | Max pixels written to each strip (180 = longest run). |
| `BRIGHTNESS_IDLE_PCT` | White idle brightness (20 %). |
| `BRIGHTNESS_ACTIVE_MIN/MAX` | Pulse min/max (0 % / 40 %). |
| `BRIGHTNESS_RESULT_PCT` | Green/red match-result brightness (40 %). |
| `STATE_TIMEOUT_MS` | Auto-reset to idle timeout (10 s). |
| `PULSE_PERIOD_MS` | Blue-pulse cycle length (1500 ms). |
| `BUTTON_DEBOUNCE_MS` | Bounce2 debounce (50 ms, per spec). |

Changing which socket pairs with which is a one-line change to `partnerOf()`
in `config.h`.