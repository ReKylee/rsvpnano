#!/usr/bin/env python3

import argparse
import os
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path


DEFAULT_ENV = "companion_api_test_waveshare_esp32s3_touch_lcd_349_rev1"
READY_PATTERN = re.compile(r"\[api-test\] ready url=(https?://\S+)")


def load_hardware_helpers(repo_root: Path):
    sys.path.insert(0, str(repo_root / "benchmark"))
    import run as helpers

    return helpers


def wait_for_base_url(helpers, repo_root: Path, environment: str, port: str, baud: str, timeout: float) -> str:
    command = helpers.monitor_command(helpers.platformio_python(repo_root), environment, port, baud)
    process = subprocess.Popen(
        command,
        cwd=repo_root,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=helpers.child_environment(),
    )
    assert process.stdout is not None
    lines: queue.Queue[str | None] = queue.Queue()

    def read_output() -> None:
        for line in process.stdout:
            lines.put(line)
        lines.put(None)

    threading.Thread(target=read_output, daemon=True).start()
    deadline = time.monotonic() + timeout
    try:
        while time.monotonic() < deadline:
            try:
                line = lines.get(timeout=min(0.5, max(0.01, deadline - time.monotonic())))
            except queue.Empty:
                continue
            if line is None:
                raise RuntimeError("serial monitor ended before the API test firmware became ready")
            print(line, end="")
            match = READY_PATTERN.search(line)
            if match:
                return match.group(1).rstrip("/")
    finally:
        if process.poll() is None:
            process.terminate()
        process.wait(timeout=5)
    raise TimeoutError(f"API test firmware did not become ready within {timeout:g} seconds")


def wait_for_api(base_url: str, timeout: float) -> None:
    print(f"[device-api] waiting for {base_url}; join the Nano Wi-Fi now if it is using access-point mode", flush=True)
    deadline = time.monotonic() + timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(f"{base_url}/api/v2/device", timeout=2) as response:
                if response.status == 200:
                    print("[device-api] device API reachable", flush=True)
                    return
        except (OSError, urllib.error.URLError) as error:
            last_error = error
        time.sleep(0.5)
    raise TimeoutError(f"device API was not reachable within {timeout:g} seconds: {last_error}")


def run_gradle_test(repo_root: Path, base_url: str, iterations: int, write: bool) -> None:
    environment = {
        **os.environ,
        "RSVPNANO_DEVICE_URL": base_url,
        "RSVPNANO_DEVICE_ITERATIONS": str(iterations),
        "RSVPNANO_DEVICE_WRITE": "1" if write else "0",
    }
    arguments = [
        ":shared:testDebugUnitTest",
        "--tests",
        "com.rsvpnano.NanoKtorClientDeviceTest",
        "--rerun-tasks",
        "--no-daemon",
        "--no-configuration-cache",
    ]
    if os.name == "nt":
        powershell = shutil.which("pwsh") or shutil.which("powershell")
        if powershell is None:
            raise RuntimeError("PowerShell is required to run the repository's local Gradle environment")
        command = [
            powershell,
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(repo_root / ".local" / "run_local_gradle.ps1"),
            *arguments,
        ]
    else:
        command = [str(repo_root / "gradlew"), *arguments]
    subprocess.run(command, cwd=repo_root, check=True, env=environment)

    report = (
        repo_root
        / "RSVPNanoCompanion"
        / "shared"
        / "build"
        / "test-results"
        / "testDebugUnitTest"
        / "TEST-com.rsvpnano.NanoKtorClientDeviceTest.xml"
    )
    if report.exists():
        output = ET.parse(report).getroot().findtext("system-out", "").strip()
        if output:
            print(output)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build, flash, and run companion API tests on a real RSVP Nano.")
    parser.add_argument("--env", default=DEFAULT_ENV, help="PlatformIO API-test environment")
    parser.add_argument("--port", default="", help="Upload/monitor serial port, for example COM3")
    parser.add_argument("--baud", default="115200", help="Serial monitor baud rate")
    parser.add_argument("--base-url", default="", help="Skip serial URL discovery and use this device URL")
    parser.add_argument("--no-upload", action="store_true", help="Use API-test firmware already on the device")
    parser.add_argument("--write", action="store_true", help="Exercise idempotent writes and a disposable book upload")
    parser.add_argument("--iterations", type=int, default=3, help="Number of complete read passes")
    parser.add_argument("--startup-timeout", type=float, default=120, help="Seconds to wait for firmware readiness")
    parser.add_argument("--api-timeout", type=float, default=60, help="Seconds to wait for network reachability")
    args = parser.parse_args()
    if args.iterations < 1:
        parser.error("--iterations must be at least 1")

    repo_root = Path(__file__).resolve().parents[2]
    helpers = load_hardware_helpers(repo_root)
    devices = helpers.platformio_devices(repo_root)
    initial_port = helpers.select_serial_port(devices, args.port)
    identity = next((helpers.device_serial(device) for device in devices if device.get("port") == initial_port), "")

    if not args.no_upload:
        if not initial_port:
            raise RuntimeError("could not identify the RSVP Nano serial port")
        helpers.run_checked(["uvx", "platformio", "run", "-e", args.env, "-j12"], repo_root)
        helpers.enter_bootloader(repo_root, initial_port)
        time.sleep(1)
        bootloader_port = helpers.wait_for_serial_port(repo_root, initial_port, identity, 15)
        helpers.run_checked(
            ["uvx", "platformio", "run", "-e", args.env, "-t", "upload", "--upload-port", bootloader_port],
            repo_root,
        )

    base_url = args.base_url.rstrip("/")
    if not base_url:
        port = helpers.wait_for_serial_port(repo_root, args.port or initial_port, identity, 30)
        base_url = wait_for_base_url(helpers, repo_root, args.env, port, args.baud, args.startup_timeout)
    wait_for_api(base_url, args.api_timeout)
    run_gradle_test(repo_root, base_url, args.iterations, args.write)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
