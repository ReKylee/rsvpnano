#!/usr/bin/env python3
"""Test production item submission, capture and drawing with the shared host canvas.

These tests stub platform assets and ASCII text services. They are not a firmware,
font-decoder or device-performance benchmark. No Context or control implementation
is replaced by a test double.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def main():
    compiler = shlex.split(os.environ.get("CXX", "g++"))
    flags = ["-std=c++23", "-Wall", "-Wextra", "-Werror", "-pedantic", "-fno-exceptions", "-fno-rtti"]
    flags += shlex.split(os.environ.get("CXXFLAGS", "")) + ["-UNDEBUG"]
    includes = ["-I"+str(HERE/"support"), "-I"+str(ROOT/"test/support"), "-I"+str(ROOT/"src")]
    with tempfile.TemporaryDirectory(prefix="rsvpnano-ui-drawing-") as folder:
        def run(name, sources, definitions=()):
            executable = Path(folder)/name
            subprocess.run(compiler + flags + list(definitions) + includes + [str(path) for path in sources]
                           + ["-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
            print(name+": passed", flush=True)
        run("draw-call", [HERE/"test_draw_call.cpp"])
        sources = [HERE/"test_items.cpp"] + [ROOT/"src/ui"/name for name in ("Ui.cpp", "Controls.cpp", "Icons.cpp")]
        run("items-direct", sources)
        run("items-aligned", sources, ["-DRSVP_BOARD_CONFIG_HEADER=1"])


if __name__ == "__main__":
    main()
