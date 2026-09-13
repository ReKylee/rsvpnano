#!/usr/bin/env python3
"""Compile the real QMI8658 implementation against a register-tracing fake bus."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="rsvpnano-imu-") as directory:
    executable = Path(directory) / "imu-tests"
    command = shlex.split(os.environ.get("CXX", "g++")) + [
        "-std=c++23", "-Wall", "-Wextra", "-Werror", "-pedantic", "-fno-exceptions", "-fno-rtti",
        "-I" + str(HERE / "support"), "-I" + str(ROOT / "src"),
        str(HERE / "test_main.cpp"), str(ROOT / "src/drivers/imu/qmi8658/Qmi8658.cpp"),
        "-o", str(executable),
    ]
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
