#!/usr/bin/env python3
"""Compile shared screen behavior with each real layout and recording UI/catalog boundaries."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="rsvpnano-interface-") as directory:
    for watch, presentation in enumerate(("regular", "watch")):
        executable = Path(directory) / presentation
        command = shlex.split(os.environ.get("CXX", "g++")) + [
            "-std=c++23", "-Wall", "-Wextra", "-Werror", "-pedantic", "-fno-exceptions", "-fno-rtti",
            *shlex.split(os.environ.get("CXXFLAGS", "")), f"-DTEST_WATCH={watch}",
            "-I" + str(HERE / "support"), "-I" + str(ROOT / "test/native_choices/support"),
            "-I" + str(ROOT / "src"), str(HERE / "test_main.cpp"),
            str(ROOT / "src/ui/screens/InterfaceSettingsScreen.cpp"),
            str(ROOT / f"src/ui/screens/{presentation}/InterfaceSettingsScreen.cpp"), "-o", str(executable),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)
