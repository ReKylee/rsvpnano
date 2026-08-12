#!/usr/bin/env python3
"""Regenerate the complete built-in and installable reader-font catalog."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
FONT_ROOT = REPO_ROOT / "fonts"
CONVERTER = FONT_ROOT / "convert_alpha4_font.py"
FALLBACK_HEADER = REPO_ROOT / "src" / "fonts" / "LiterataFallbackAlpha4.h"


@dataclass(frozen=True, slots=True)
class FontPreset:
    id: str
    name: str
    source: Path
    codepoint_map: str | None = None
    locales: str = ""
    shaping: bool = False
    header: bool = False


PRESETS = (
    FontPreset(
        "literata-fallback",
        "LiterataFallbackAlpha4",
        REPO_ROOT / "third_party" / "literata" / "Literata-Italic[opsz,wght].ttf",
        header=True,
    ),
    FontPreset(
        "amiri",
        "Amiri",
        REPO_ROOT / "third_party" / "amiri" / "Amiri-Regular.ttf",
        "Arab",
        "ar",
        shaping=True,
    ),
    FontPreset("andika", "Andika", REPO_ROOT / "third_party" / "andika" / "Andika-Regular.ttf"),
    FontPreset(
        "atkinson-hyperlegible",
        "Atkinson Hyperlegible",
        REPO_ROOT / "third_party" / "atkinson-hyperlegible" / "AtkinsonHyperlegible-Regular.ttf",
    ),
    FontPreset(
        "courier-prime",
        "Courier Prime",
        REPO_ROOT / "third_party" / "courier-prime" / "CourierPrime-Regular.ttf",
    ),
    FontPreset(
        "frank-ruhl-libre",
        "Frank Ruhl Libre",
        REPO_ROOT / "third_party" / "frank-ruhl-libre" / "FrankRuhlLibre[wght].ttf",
        "Hebr",
        "he",
        shaping=True,
    ),
    FontPreset(
        "noto-naskh-arabic",
        "Noto Naskh Arabic",
        REPO_ROOT / "third_party" / "noto-naskh-arabic" / "NotoNaskhArabic[wght].ttf",
        "Arab",
        "ar",
        shaping=True,
    ),
    FontPreset(
        "noto-serif-hebrew",
        "Noto Serif Hebrew",
        REPO_ROOT / "third_party" / "noto-serif-hebrew" / "NotoSerifHebrew[wdth,wght].ttf",
        "Hebr",
        "he",
        shaping=True,
    ),
    FontPreset(
        "noto-serif-japanese",
        "Noto Serif Japanese",
        REPO_ROOT / "third_party" / "noto-serif-japanese" / "NotoSerifJP-Regular.otf",
        "Jpan",
        "ja",
    ),
    FontPreset(
        "noto-serif-simplified-chinese",
        "Noto Serif Simplified Chinese",
        REPO_ROOT / "third_party" / "noto-serif-simplified-chinese" / "NotoSerifSC-Regular.otf",
        "Hani",
        "zh-Hans",
    ),
    FontPreset(
        "opendyslexic",
        "OpenDyslexic",
        REPO_ROOT / "third_party" / "opendyslexic" / "OpenDyslexic-Regular.ttf",
    ),
    FontPreset(
        "stix-two-math",
        "STIX Two Math",
        REPO_ROOT / "third_party" / "stix-two-math" / "STIXTwoMath-Regular.ttf",
        "Zmth",
    ),
)


def converter_command(
    preset: FontPreset,
    catalog_root: Path,
    fallback_header: Path,
    sizes: str | None = None,
) -> list[str]:
    command = [
        sys.executable,
        str(CONVERTER),
        "--font",
        str(preset.source),
        "--name",
        preset.name,
    ]
    if preset.header:
        command.extend(("--header", "--output", str(fallback_header)))
    else:
        command.extend(("--output-root", str(catalog_root)))
    if preset.codepoint_map:
        command.extend(("--map", preset.codepoint_map))
    if preset.locales:
        command.extend(("--locales", preset.locales))
    if preset.shaping:
        command.append("--shaping")
    if sizes:
        command.extend(("--sizes", sizes))
    return command


def selected_presets(ids: list[str]) -> tuple[FontPreset, ...]:
    if not ids:
        return PRESETS
    unknown = set(ids) - {preset.id for preset in PRESETS}
    if unknown:
        raise ValueError(f"unknown font preset(s): {', '.join(sorted(unknown))}")
    selected = set(ids)
    return tuple(preset for preset in PRESETS if preset.id in selected)


def read_catalog(path: Path) -> list[dict[str, object]]:
    if not path.exists():
        return []
    return json.loads(path.read_text(encoding="utf-8"))


def write_catalog(path: Path, catalog: list[dict[str, object]]) -> None:
    temporary = path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(catalog, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def publish(selected: tuple[FontPreset, ...], generated_root: Path, generated_header: Path) -> None:
    catalog_presets = tuple(preset for preset in selected if not preset.header)
    generated_catalog = read_catalog(generated_root / "index.json")
    all_catalog_ids = {preset.id for preset in PRESETS if not preset.header}
    selected_catalog_ids = {preset.id for preset in catalog_presets}

    if selected_catalog_ids == all_catalog_ids:
        catalog = generated_catalog
    else:
        catalog = [
            entry
            for entry in read_catalog(FONT_ROOT / "index.json")
            if entry.get("id") not in selected_catalog_ids
        ]
        catalog.extend(generated_catalog)
        catalog.sort(key=lambda entry: str(entry.get("name", "")).lower())

    for preset in catalog_presets:
        source = generated_root / preset.name / "font.rfont4"
        destination = FONT_ROOT / preset.name / "font.rfont4"
        destination.parent.mkdir(parents=True, exist_ok=True)
        os.replace(source, destination)
        print(f"published {destination.relative_to(REPO_ROOT)}")
    if catalog_presets:
        write_catalog(FONT_ROOT / "index.json", catalog)
        print("published fonts/index.json")
    if any(preset.header for preset in selected):
        os.replace(generated_header, FALLBACK_HEADER)
        print(f"published {FALLBACK_HEADER.relative_to(REPO_ROOT)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("presets", nargs="*", metavar="FONT", help="preset ids; defaults to the complete catalog")
    parser.add_argument("--sizes", help="override the converter's named strike sizes")
    parser.add_argument("--list", action="store_true", help="list available preset ids and exit")
    args = parser.parse_args()

    if args.list:
        for preset in PRESETS:
            print(preset.id)
        return 0

    try:
        selected = selected_presets(args.presets)
    except ValueError as error:
        parser.error(str(error))

    missing = [preset.source for preset in selected if not preset.source.is_file()]
    if missing:
        parser.error("missing source font(s):\n  " + "\n  ".join(str(path) for path in missing))

    with tempfile.TemporaryDirectory(prefix=".generate-fonts-", dir=FONT_ROOT) as directory:
        temporary = Path(directory)
        generated_root = temporary / "catalog"
        generated_header = temporary / FALLBACK_HEADER.name
        for preset in selected:
            print(f"generating {preset.id}", flush=True)
            subprocess.run(
                converter_command(preset, generated_root, generated_header, args.sizes),
                cwd=REPO_ROOT,
                check=True,
            )
        publish(selected, generated_root, generated_header)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
