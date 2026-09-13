#!/usr/bin/env python3
"""Exercise the real driver and its source-selected adapter with a fake I2C bus."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
DRIVER = ROOT / "src/drivers/imu/qmi8658"


def main():
    compiler = shlex.split(os.environ.get("CXX", "g++"))
    flags = ["-std=c++23", "-Wall", "-Wextra", "-Werror", "-pedantic",
             "-fno-exceptions", "-fno-rtti"]
    flags += shlex.split(os.environ.get("CXXFLAGS", ""))
    with tempfile.TemporaryDirectory(prefix="rsvpnano-imu-") as directory:
        temporary = Path(directory)
        includes = ["-I" + str(temporary), "-I" + str(HERE / "support"),
                    "-I" + str(ROOT / "src")]

        def build(name, sources, extra=()):
            executable = temporary / name
            subprocess.run(compiler + flags + includes + list(extra)
                           + [str(source) for source in sources] + ["-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

        build("driver", [HERE / "test_main.cpp", DRIVER / "Qmi8658.cpp"])
        build("no-imu", [HERE / "test_no_imu.cpp"])
        for controllers, bus in ((1, 0), (2, 0), (2, 1)):
            (temporary / "ImuConfig.h").write_text(
                "#pragma once\n#include <cstdint>\nnamespace Board::Config {\n"
                f"constexpr int IMU_I2C_BUS = {bus};\n"
                "namespace Imu { constexpr uint8_t kAddress = 0x6B; "
                "constexpr bool kReleaseBusBeforeRead = true; }\n}\n")
            extra = [f"-DSOC_I2C_NUM={controllers}", f"-DTEST_IMU_BUS={bus}",
                     '-DRSVP_BOARD_CONFIG_HEADER="ImuConfig.h"']
            build(f"board-{controllers}-{bus}",
                  [HERE / "test_board.cpp", DRIVER / "Qmi8658.cpp", DRIVER / "BoardImu.cpp"], extra)

        # Reject a selected bus that the target cannot provide, at compile time.
        result = subprocess.run(compiler + flags + includes + ["-DSOC_I2C_NUM=1",
                                '-DRSVP_BOARD_CONFIG_HEADER="ImuConfig.h"',
                                "-c", str(DRIVER / "BoardImu.cpp"), "-o", str(temporary / "invalid.o")],
                                capture_output=True, text=True)
        if result.returncode == 0 or "Selected IMU I2C controller is unavailable" not in result.stderr:
            raise RuntimeError("Invalid bus selection did not produce the expected diagnostic")
    print("IMU source selection: no-device link and unavailable-controller rejection passed")


if __name__ == "__main__":
    main()
