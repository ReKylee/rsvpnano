#!/usr/bin/env python3
"""Test settings edit semantics using a recording control sink, without pixel rendering."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="rsvpnano-choices-") as directory:
    executable = Path(directory) / "choice-tests"
    command = shlex.split(os.environ.get("CXX", "g++")) + [
        "-std=c++23", "-Wall", "-Wextra", "-Werror", "-pedantic", "-fno-exceptions", "-fno-rtti",
        "-I" + str(HERE / "support"), "-I" + str(ROOT / "src"),
        str(HERE / "test_main.cpp"),
        "-o", str(executable),
    ]
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
