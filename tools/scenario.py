"""Play a scripted game through the live ESP32 so someone on site can watch the boxes.

Run on the Raspberry Pi. Presses are simulated over USB serial with vonk-ops'
``System.esp_send`` (safe: never toggles DTR/RTS, so the ESP is not reset).
The ESP's replies go to the serial logger; follow them in a second shell:

    tail -f ~/logs/memory-game-$(date +%F).log

Usage:

    python3 scenario.py [all|wrong|pair|timeout|win|idle|styles]
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path("~/vonk-ops").expanduser()))
import museum_mode as mm  # noqa: E402

PAIRS = ["1A 1B", "2A 2B", "3A 3B", "4A 4B", "5A 5B"]

_system = mm.System(mm.load_config(mm.DEFAULT_CONFIG_PATH))


def send(command: str, expect: str = "", wait: float = 0.0) -> None:
    print(f"> {command:<12} {expect}")
    if not _system.esp_send(command):
        sys.exit(f"could not send {command!r} to the ESP32")
    time.sleep(wait)


def wrong() -> None:
    send("RESET", "all white", 2)
    send("1A", "1A solid blue, other boxes invite", 5)
    send("2A", "1A + 2A pulse blue, then red 2.5 s, then white", 5)


def pair() -> None:
    send("RESET", "all white", 2)
    send("1A", "1A blue, others invite", 3)
    send("1B", "1A + 1B pulse blue, then green (stays)", 4)
    send("1A", "ignored: 1A stays green", 2)


def timeout() -> None:
    send("RESET", "all white", 2)
    send("2A", "2A blue, others invite; back to white after 15 s", 17)


def win() -> None:
    send("RESET", "all white", 2)
    for p in PAIRS:
        a, b = p.split()
        send(a, f"{a} blue", 1.5)
        send(b, f"{a} + {b} pulse, green", 2)
    print("  expect: 1 s green, ~4 s green <-> white, then all white")
    time.sleep(7)


def idle() -> None:
    send("RESET", "all white", 2)
    send("3A", "3A blue", 1.5)
    send("3B", "3A + 3B green", 2)
    print("  waiting 62 s without presses: 3A + 3B go white at ~60 s")
    time.sleep(62)


def styles() -> None:
    for style in ("WHITE", "YELLOW", "BLINK"):
        send(f"STYLE {style}", "", 1)
        send("RESET", "all white", 2)
        send("1A", f"1A blue, others invite in style {style}", 10)
    send("RESET", "all white", 1)
    print("  note: the style stays BLINK until 'STYLE <name>' or an ESP reset")


SCENARIOS = {"wrong": wrong, "pair": pair, "timeout": timeout, "win": win,
             "idle": idle, "styles": styles}


def main() -> int:
    which = sys.argv[1] if len(sys.argv) > 1 else "all"
    if which not in SCENARIOS and which != "all":
        print(__doc__)
        return 2
    for name, fn in SCENARIOS.items():
        if which in (name, "all"):
            print(f"== {name}")
            fn()
    send("STATUS", "see the log for loop_max_ms / led_push_per_s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
