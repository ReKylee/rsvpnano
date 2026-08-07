#!/usr/bin/env python3
"""Generate installable UI locale packs from the canonical localization source."""

from __future__ import annotations

import argparse
import ast
import hashlib
import io
import json
import re
import struct
import sys
import zipfile
from datetime import datetime
from pathlib import Path

from generate_localization import DEFAULT_TOML, Language, LocalizationModel, load_model

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT = REPO_ROOT / "locale-packs"
DEFAULT_BUILTIN_UI_FONT = REPO_ROOT / "src" / "fonts" / "UiFont6x9.h"
MAX_UI_FONT_BYTES = 64 * 1024


def compiled_u8g2_font(path: Path) -> bytes:
	text = path.read_text(encoding="utf-8")
	literals = re.findall(r'"(?:[^"\\]|\\.)*"', text.split("=", 1)[-1])
	if not literals:
		raise ValueError(f"{path} contains no U8g2 font data")
	# C string arrays include one final NUL beyond their explicit string contents.
	return b"".join(ast.literal_eval("b" + literal) for literal in literals) + b"\0"


def u8g2_codepoints(font: bytes) -> set[int]:
	if len(font) < 25 or len(font) > MAX_UI_FONT_BYTES:
		raise ValueError("UI font has an invalid length")

	def be16(offset: int) -> int:
		if offset + 2 > len(font):
			raise ValueError("UI font lookup is truncated")
		return int.from_bytes(font[offset : offset + 2], "big")

	codepoints: set[int] = set()
	position = 23
	while True:
		if position + 2 > len(font):
			raise ValueError("UI font ASCII records are truncated")
		size = font[position + 1]
		if size == 0:
			break
		if size < 2 or position + size > len(font):
			raise ValueError("UI font has an invalid ASCII record")
		codepoints.add(font[position])
		position += size

	position = 23 + be16(21)
	while True:
		if position + 4 > len(font):
			raise ValueError("UI font Unicode lookup is truncated")
		last = be16(position + 2)
		position += 4
		if last == 0xFFFF:
			break

	while True:
		if position + 2 > len(font):
			raise ValueError("UI font Unicode records are truncated")
		codepoint = be16(position)
		if codepoint == 0:
			return codepoints
		if position + 3 > len(font):
			raise ValueError("UI font Unicode record is truncated")
		size = font[position + 2]
		if size < 3 or position + size > len(font):
			raise ValueError("UI font has an invalid Unicode record")
		codepoints.add(codepoint)
		position += size


def required_ui_codepoints(model: LocalizationModel, language: Language) -> set[int]:
	text = ".?" + language.label + "".join(entry.values.get(language.name, "") for entry in model.texts)
	return {ord(character) for character in text if character not in "\r\n"}


def ui_font(model: LocalizationModel, language: Language, built_in: set[int]) -> tuple[bytes, str] | None:
	required = required_ui_codepoints(model, language)
	if language.ui_font is None:
		missing = required - built_in
		if missing:
			formatted = ", ".join(f"U+{codepoint:04X}" for codepoint in sorted(missing)[:8])
			raise ValueError(f"{language.name} needs ui_font for {formatted}")
		return None

	source = Path(language.ui_font.source)
	if not source.is_absolute():
		source = REPO_ROOT / source
	font = source.read_bytes()
	codepoints = u8g2_codepoints(font)
	missing = required - codepoints
	if missing:
		formatted = ", ".join(f"U+{codepoint:04X}" for codepoint in sorted(missing)[:8])
		raise ValueError(f"{language.name} UI font is missing {formatted}")
	return font, language.ui_font.license


def string_table(model: LocalizationModel, language: Language) -> bytes:
	text = bytearray()
	offsets = [0]
	for entry in model.texts:
		text.extend(entry.values.get(language.name, "").encode("utf-8"))
		offsets.append(len(text))
	header = b"RSL1" + struct.pack("<HHI", 1, len(model.texts), len(text))
	return header + struct.pack(f"<{len(offsets)}I", *offsets) + text


def engine_requirements(language: Language) -> list[str]:
	requires: list[str] = []
	if language.direction == "rtl":
		requires.append("bidi")
	if "Arab" in language.scripts:
		requires.append("shaping.opentype")
	return requires


def manifest(
	language: Language,
	strings: bytes,
	font: tuple[bytes, str] | None = None,
) -> str:
	scripts = ", ".join(f'"{script}"' for script in language.scripts)
	requires = engine_requirements(language)
	required_capabilities = ", ".join(json.dumps(capability) for capability in requires)
	result = f'''schema_version = 2
id = {json.dumps(language.code, ensure_ascii=False)}
version = "1.0.0"
locale = {json.dumps(language.code, ensure_ascii=False)}
native_name = {json.dumps(language.label, ensure_ascii=False)}
english_name = {json.dumps(language.name, ensure_ascii=False)}
direction = "{language.direction}"
scripts = [{scripts}]
unicode_version = "17.0.0"
translation_status = "{language.translation_status}"
minimum_firmware = "0.0.9"
engine_abi = 1
requires = [{required_capabilities}]

[ui.strings]
path = "ui/strings.bin"
bytes = {len(strings)}
sha256 = "{hashlib.sha256(strings).hexdigest()}"
license = "MIT"
'''
	if font:
		font_bytes, license = font
		result += f'''
[ui.font]
path = "ui/font.u8g2"
bytes = {len(font_bytes)}
sha256 = "{hashlib.sha256(font_bytes).hexdigest()}"
license = {json.dumps(license, ensure_ascii=False)}
'''
	return result


def zip_overlay(files: dict[str, bytes], generated_at: tuple[int, int, int, int, int, int]) -> bytes:
	buffer = io.BytesIO()
	with zipfile.ZipFile(buffer, "w") as archive:
		for path, content in sorted(files.items()):
			info = zipfile.ZipInfo(path, date_time=generated_at)
			info.compress_type = zipfile.ZIP_DEFLATED
			info.create_system = 3
			info.external_attr = 0o100644 << 16
			archive.writestr(info, content, compresslevel=9)
	return buffer.getvalue()


def outputs(
	model: LocalizationModel,
	root: Path,
	generated_at: tuple[int, int, int, int, int, int],
) -> dict[Path, bytes]:
	result: dict[Path, bytes] = {}
	built_in = u8g2_codepoints(compiled_u8g2_font(DEFAULT_BUILTIN_UI_FONT))
	for language in model.languages:
		if language.name == model.default_language:
			continue
		strings = string_table(model, language)
		font = ui_font(model, language, built_in)
		files = {
			f"locales/{language.code}/manifest.toml": manifest(language, strings, font).encode("utf-8"),
			f"locales/{language.code}/ui/strings.bin": strings,
		}
		if font:
			files[f"locales/{language.code}/ui/font.u8g2"] = font[0]
		result[root / f"{language.code}.zip"] = zip_overlay(files, generated_at)
	return result


def same_output(path: Path, expected: bytes) -> bool:
	if not path.exists():
		return False
	try:
		with zipfile.ZipFile(path) as actual, zipfile.ZipFile(io.BytesIO(expected)) as wanted:
			return actual.namelist() == wanted.namelist() and all(
				actual.read(name) == wanted.read(name) for name in actual.namelist()
			)
	except zipfile.BadZipFile:
		return False


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--toml", type=Path, default=DEFAULT_TOML)
	parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
	parser.add_argument("--check", action="store_true")
	args = parser.parse_args()

	try:
		now = datetime.now()
		generated_at = (now.year, now.month, now.day, now.hour, now.minute, now.second & ~1)
		generated = outputs(load_model(args.toml), args.output, generated_at)
		stale = False
		for path, content in generated.items():
			if args.check:
				if not same_output(path, content):
					print(f"out of date: {path}", file=sys.stderr)
					stale = True
				continue
			path.parent.mkdir(parents=True, exist_ok=True)
			path.write_bytes(content)
			print(f"wrote {path}")
		return 1 if stale else 0
	except Exception as exc:
		print(f"error: {exc}", file=sys.stderr)
		return 1


if __name__ == "__main__":
	raise SystemExit(main())
