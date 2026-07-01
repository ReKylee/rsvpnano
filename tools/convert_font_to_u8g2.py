#!/usr/bin/env python3
"""Convert a font file into a U8g2 C header for RSVP Nano.

This script intentionally uses U8g2's own `bdfconv` tool for the final
U8g2 font encoding. For TTF/OTF input, it first uses `otf2bdf` to rasterize
at a fixed pixel size, then passes the BDF through upstream U8g2 `bdfconv`.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

DEFAULT_MAP = "32-126,8211,8212,8216,8217,8220,8221,8226,8230"
DEFAULT_SIZE = 18
DEFAULT_TMP_DIR = Path(".tmp/fonts")


def run(command: list[str]) -> None:
    print("+", " ".join(command))
    subprocess.run(command, check=True)


def require_existing_file(path: Path, label: str) -> Path:
    if not path.is_file():
        raise SystemExit(f"error: {label} not found: {path}")
    return path


def sanitize_identifier(value: str) -> str:
    value = re.sub(r"[^0-9A-Za-z_]+", "_", value.strip())
    value = value.strip("_") or "font"
    if value[0].isdigit():
        value = f"font_{value}"
    return value.lower()


def default_symbol(font_path: Path, size: int) -> str:
    stem = sanitize_identifier(font_path.stem)
    return f"u8g2_font_rsvpnano_{stem}_{size}_tf"


def default_output(font_path: Path) -> Path:
    # Keep the checked-in header names readable while still being generic.
    cleaned = re.sub(r"[^0-9A-Za-z]+", " ", font_path.stem).title().replace(" ", "")
    cleaned = cleaned or "GeneratedFont"
    return Path("src/fonts") / f"{cleaned}U8g2.h"


def find_bdfconv(explicit: str | None, u8g2_repo: Path | None) -> Path | str:
    if explicit:
        return str(require_existing_file(Path(explicit), "bdfconv"))

    env_value = os.environ.get("BDFCONV")
    if env_value:
        return str(require_existing_file(Path(env_value), "BDFCONV"))

    repo_candidates: list[Path] = []
    if u8g2_repo is not None:
        repo_candidates.append(u8g2_repo)
    if os.environ.get("U8G2_REPO"):
        repo_candidates.append(Path(os.environ["U8G2_REPO"]))

    repo_candidates.extend([
        Path("third_party/u8g2"),
        Path("external/u8g2"),
        Path(".tmp/u8g2"),
    ])

    for repo in repo_candidates:
        tool_dir = repo / "tools/font/bdfconv"
        for candidate in (tool_dir / "bdfconv", tool_dir / "bdfconv.exe"):
            if candidate.is_file():
                return str(candidate)

    path_value = shutil.which("bdfconv")
    if path_value:
        return path_value

    raise SystemExit(
        "error: bdfconv not found. Build U8g2's tools/font/bdfconv/bdfconv "
        "and pass --bdfconv, set BDFCONV, or pass --u8g2-repo."
    )


def find_otf2bdf(explicit: str | None) -> str:
    if explicit:
        return str(require_existing_file(Path(explicit), "otf2bdf"))

    env_value = os.environ.get("OTF2BDF")
    if env_value:
        return str(require_existing_file(Path(env_value), "OTF2BDF"))

    path_value = shutil.which("otf2bdf")
    if path_value:
        return path_value

    raise SystemExit("error: otf2bdf not found. Install it or pass --otf2bdf.")


def build_bdfconv(u8g2_repo: Path) -> None:
    tool_dir = u8g2_repo / "tools/font/bdfconv"
    if not tool_dir.is_dir():
        raise SystemExit(f"error: U8g2 bdfconv directory not found: {tool_dir}")
    run(["make", "-C", str(tool_dir)])


def convert_to_bdf(font_path: Path, bdf_path: Path, size: int, otf2bdf: str) -> None:
    suffix = font_path.suffix.lower()
    if suffix == ".bdf":
        shutil.copyfile(font_path, bdf_path)
        return

    if suffix not in {".ttf", ".otf", ".ttc"}:
        raise SystemExit(
            f"error: unsupported input extension '{font_path.suffix}'. "
            "Use .ttf, .otf, .ttc, or .bdf."
        )

    run([otf2bdf, "-p", str(size), "-r", "72", "-o", str(bdf_path), str(font_path)])


def wrap_generated_header(
    generated_header: Path,
    output_header: Path,
    *,
    font_label: str,
    source_path: Path,
    size: int,
    glyph_map: str,
    license_note: str | None,
) -> None:
    output_header.parent.mkdir(parents=True, exist_ok=True)

    generated_lines = generated_header.read_text(encoding="utf-8").splitlines()
    generated_lines = [line for line in generated_lines if not line.startswith("#include")]

    lines = [
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        f"// Generated from {font_label} for RSVP Nano.",
        f"// Source: {source_path}",
        f"// Size: {size}px",
        f"// U8g2 map: {glyph_map}",
    ]
    if license_note:
        lines.append(f"// {license_note}")
    lines.extend(["", *generated_lines, ""])

    output_header.write_text("\n".join(lines), encoding="utf-8")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert TTF/OTF/BDF fonts to U8g2 C headers using U8g2 bdfconv.",
    )
    parser.add_argument("font", type=Path, help="Input .ttf, .otf, .ttc, or .bdf font file")
    parser.add_argument("--out", type=Path, help="Output header path")
    parser.add_argument("--symbol", help="Exported U8g2 font symbol")
    parser.add_argument("--size", type=int, default=DEFAULT_SIZE, help="Pixel size for TTF/OTF rasterization")
    parser.add_argument("--map", default=DEFAULT_MAP, help="bdfconv glyph map")
    parser.add_argument("--font-label", help="Human-readable font name for generated comments")
    parser.add_argument("--license-note", help="Short license/attribution comment for the generated header")
    parser.add_argument("--tmp-dir", type=Path, default=DEFAULT_TMP_DIR, help="Temporary build directory")
    parser.add_argument("--bdfconv", help="Path to U8g2 bdfconv")
    parser.add_argument("--otf2bdf", help="Path to otf2bdf")
    parser.add_argument("--u8g2-repo", type=Path, help="Path to an upstream U8g2 checkout")
    parser.add_argument("--build-bdfconv", action="store_true", help="Run make in <u8g2-repo>/tools/font/bdfconv first")
    parser.add_argument("--bdfconv-format", default="1", help="bdfconv -f value; U8g2 font format defaults to 1")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    font_path = require_existing_file(args.font, "font")
    output_header = args.out or default_output(font_path)
    symbol = args.symbol or default_symbol(font_path, args.size)
    font_label = args.font_label or font_path.stem

    if args.build_bdfconv:
        if args.u8g2_repo is None:
            raise SystemExit("error: --build-bdfconv requires --u8g2-repo")
        build_bdfconv(args.u8g2_repo)

    bdfconv = find_bdfconv(args.bdfconv, args.u8g2_repo)
    otf2bdf = find_otf2bdf(args.otf2bdf)

    args.tmp_dir.mkdir(parents=True, exist_ok=True)
    safe_symbol = sanitize_identifier(symbol)
    bdf_path = args.tmp_dir / f"{safe_symbol}-{args.size}.bdf"
    raw_header = args.tmp_dir / f"{safe_symbol}.generated.h"

    convert_to_bdf(font_path, bdf_path, args.size, otf2bdf)
    run([
        str(bdfconv),
        "-f",
        str(args.bdfconv_format),
        "-m",
        args.map,
        "-n",
        symbol,
        str(bdf_path),
        "-o",
        str(raw_header),
    ])

    wrap_generated_header(
        raw_header,
        output_header,
        font_label=font_label,
        source_path=font_path,
        size=args.size,
        glyph_map=args.map,
        license_note=args.license_note,
    )

    print(f"wrote {output_header}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
