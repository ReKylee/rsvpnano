#!/usr/bin/env python3
"""Compile real UI headers/screens with recording primitives, not a replacement Context class.

Font assets, platform graphics and unrelated application owners are stubbed. These
are host API/composition tests; they do not validate pixels, touch capture or hardware.
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
    flags += shlex.split(os.environ.get("CXXFLAGS", ""))
    flags += ["-UNDEBUG"]
    includes = ["-I" + str(HERE / "support"), "-I" + str(ROOT / "src"), "-I" + str(HERE)]
    with tempfile.TemporaryDirectory(prefix="rsvpnano-ui-inputs-") as directory:
        def build(name, sources, platform_stubs=True):
            executable = Path(directory) / name
            include_dirs = includes if platform_stubs else ["-I" + str(ROOT / "src")]
            subprocess.run(compiler + flags + include_dirs + [str(source) for source in sources]
                           + ["-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

        build("screen-data-headers", [HERE / "test_headers.cpp"], platform_stubs=False)
        build("geometry", [HERE / "test_geometry.cpp"])
        build("inputs", [HERE / "test_inputs.cpp", HERE / "Recording.cpp"])
        build("input-edges", [HERE / "test_input_edges.cpp", HERE / "Recording.cpp"])
        build("input-lifetimes", [HERE / "test_input_lifetimes.cpp", HERE / "Recording.cpp"])
        for profile in ("regular", "watch"):
            screens = ROOT / "src/ui/screens" / profile
            build(profile, [HERE / "test_screens.cpp", HERE / "Recording.cpp",
                            screens / "ReadingSettingsScreen.cpp", screens / "SettingsScreen.cpp",
                            screens / "ScreenCommon.cpp"])


if __name__ == "__main__":
    main()
