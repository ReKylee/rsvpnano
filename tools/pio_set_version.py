Import("env")

import json
import os
import subprocess
from pathlib import Path


PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))


def detect_version() -> str:
    override = os.environ.get("RSVP_FIRMWARE_VERSION", "").strip()
    if override:
        return override

    try:
        value = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=PROJECT_DIR,
            text=True,
        ).strip()
        return value or "dev"
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "dev"


version = detect_version()
generated_dir = Path(env.subst("$BUILD_DIR")) / "generated"
generated_header = generated_dir / "FirmwareVersion.generated.h"
contents = f"#pragma once\n\ninline constexpr char kFirmwareVersion[] = {json.dumps(version)};\n"

generated_dir.mkdir(parents=True, exist_ok=True)
if not generated_header.exists() or generated_header.read_text(encoding="utf-8") != contents:
    generated_header.write_text(contents, encoding="utf-8")

env.AppendUnique(CPPPATH=[str(generated_dir)])
