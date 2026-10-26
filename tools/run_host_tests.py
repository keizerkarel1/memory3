"""Compile and run the host unit tests for the pure game logic (src/game_logic.cpp).

Needs a C++17 compiler: g++ or clang++ on PATH, or the pip package ``ziglang``
(``pip install ziglang`` — works without admin rights on a managed laptop).

    python tools/run_host_tests.py
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCES = [ROOT / "src" / "game_logic.cpp", ROOT / "test" / "host" / "test_game_logic.cpp"]


def find_compiler() -> list[str]:
    for name in ("g++", "clang++"):
        if shutil.which(name):
            return [name]
    try:
        import ziglang  # noqa: F401
    except ImportError:
        sys.exit("No C++ compiler found: install g++/clang++ or run `pip install ziglang`.")
    # zig's bundled Windows headers trigger -Wnullability-completeness noise.
    return [sys.executable, "-m", "ziglang", "c++", "-Wno-nullability-completeness"]


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / ("host_tests.exe" if sys.platform == "win32" else "host_tests")
        cmd = find_compiler() + [
            "-std=c++17", "-O1", "-Wall", "-Wextra",
            "-I", str(ROOT / "src"),
            *map(str, SOURCES),
            "-o", str(exe),
        ]
        build = subprocess.run(cmd)
        if build.returncode != 0:
            return build.returncode
        return subprocess.run([str(exe)]).returncode


if __name__ == "__main__":
    sys.exit(main())
