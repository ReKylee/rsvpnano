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
import unicodedata
import zipfile
from datetime import datetime
from pathlib import Path

from generate_localization import DEFAULT_TOML, Language, LocalizationModel, load_model
from u8g2_font import encode_bdf, encode_outline

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT = REPO_ROOT / "locale-packs"
DEFAULT_BUILTIN_UI_FONT = REPO_ROOT / "src" / "fonts" / "UiFont6x9.h"
MAX_UI_FONT_BYTES = 64 * 1024
PACK_VERSION = "1.0.1"


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


def source_path(value: str) -> Path:
	path = Path(value)
	return path if path.is_absolute() else REPO_ROOT / path


def _shape_arabic_strings(strings: list[str], font_path: Path, language: str) -> list[str]:
	try:
		import uharfbuzz as hb
		from fontTools.ttLib import TTFont
	except ImportError as exc:
		raise RuntimeError("Arabic UI generation requires uharfbuzz and fonttools") from exc

	font_bytes = font_path.read_bytes()
	face = hb.Face(font_bytes)
	font = hb.Font(face)
	font.scale = face.upem, face.upem
	tt_font = TTFont(font_path, lazy=True)
	reverse_cmap: dict[int, set[int]] = {}
	for table in tt_font["cmap"].tables:
		if table.isUnicode():
			for codepoint, glyph_name in table.cmap.items():
				reverse_cmap.setdefault(tt_font.getGlyphID(glyph_name), set()).add(codepoint)
	tt_font.close()

	def mapped_codepoint(glyph_id: int) -> int:
		candidates = reverse_cmap.get(glyph_id, set())
		bmp = [codepoint for codepoint in candidates if codepoint <= 0xFFFF]
		if not bmp:
			raise ValueError(f"{font_path} shaped glyph {glyph_id} has no BMP Unicode mapping")
		return min(
			bmp,
			key=lambda codepoint: (
				0 if 0xFB50 <= codepoint <= 0xFDFF or 0xFE70 <= codepoint <= 0xFEFF else 1,
				codepoint,
			),
		)

	def shape_run(run: str) -> str:
		buffer = hb.Buffer()
		buffer.add_str(run)
		buffer.direction = "rtl"
		buffer.script = "arab"
		buffer.language = language
		hb.shape(font, buffer)
		positions = buffer.glyph_positions
		if any(position.x_offset or position.y_offset for position in positions):
			raise ValueError("Arabic UI text uses positioned marks that U8g2 cannot preserve")
		return "".join(chr(mapped_codepoint(info.codepoint)) for info in reversed(buffer.glyph_infos))

	result: list[str] = []
	for text in strings:
		shaped: list[str] = []
		index = 0
		while index < len(text):
			if unicodedata.bidirectional(text[index]) != "AL":
				shaped.append(text[index])
				index += 1
				continue
			end = index + 1
			while end < len(text) and unicodedata.bidirectional(text[end]) in {"AL", "NSM", "BN"}:
				end += 1
			shaped.append(shape_run(text[index:end]))
			index = end
		result.append("".join(shaped))
	return result


def ui_strings(model: LocalizationModel, language: Language) -> list[str]:
	strings = [entry.values.get(language.name, "") for entry in model.texts]
	if "Arab" not in language.scripts:
		return strings
	if language.ui_font is None or language.ui_font.shaping_source is None:
		raise ValueError(f"{language.name} requires ui_font.shaping_source")
	return _shape_arabic_strings(strings, source_path(language.ui_font.shaping_source), language.code)


def required_ui_codepoints(strings: list[str]) -> set[int]:
	text = ".?" + "".join(strings)
	return {ord(character) for character in text if character not in "\r\n"}


def ui_font(language: Language, required: set[int], built_in: set[int]) -> tuple[bytes, str] | None:
	if language.ui_font is None:
		missing = required - built_in
		if missing:
			formatted = ", ".join(f"U+{codepoint:04X}" for codepoint in sorted(missing)[:8])
			raise ValueError(f"{language.name} needs ui_font for {formatted}")
		return None

	source = source_path(language.ui_font.source)
	suffix = source.suffix.lower()
	if suffix == ".bdf":
		font = encode_bdf(source, required)
	elif suffix in {".ttf", ".otf"}:
		if language.ui_font.pixel_size is None:
			raise ValueError(f"{language.name} outline UI font requires pixel_size")
		font = encode_outline(source, required, language.ui_font.pixel_size)
	else:
		font = source.read_bytes()
	codepoints = u8g2_codepoints(font)
	missing = required - codepoints
	if missing:
		formatted = ", ".join(f"U+{codepoint:04X}" for codepoint in sorted(missing)[:8])
		raise ValueError(f"{language.name} UI font is missing {formatted}")
	return font, language.ui_font.license


def string_table(strings: list[str]) -> bytes:
	text = bytearray()
	offsets = [0]
	for value in strings:
		text.extend(value.encode("utf-8"))
		offsets.append(len(text))
	header = b"RSL1" + struct.pack("<HHI", 1, len(strings), len(text))
	return header + struct.pack(f"<{len(offsets)}I", *offsets) + text


def engine_requirements(language: Language) -> list[str]:
	requires: list[str] = []
	if language.direction == "rtl":
		requires.append("bidi")
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
version = "{PACK_VERSION}"
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
	catalog: list[dict[str, object]] = []
	built_in = u8g2_codepoints(compiled_u8g2_font(DEFAULT_BUILTIN_UI_FONT))
	for language in model.languages:
		if language.name == model.default_language:
			continue
		prepared_strings = ui_strings(model, language)
		required = required_ui_codepoints([*prepared_strings, language.label])
		strings = string_table(prepared_strings)
		font = ui_font(language, required, built_in)
		files = {
			f"locales/{language.code}/manifest.toml": manifest(language, strings, font).encode("utf-8"),
			f"locales/{language.code}/ui/strings.bin": strings,
		}
		if font:
			files[f"locales/{language.code}/ui/font.u8g2"] = font[0]
		result[root / f"{language.code}.zip"] = zip_overlay(files, generated_at)
		catalog.append(
			{
				"id": language.code,
				"name": language.label,
				"englishName": language.name,
				"version": PACK_VERSION,
				"locale": language.code,
				"direction": language.direction,
				"scripts": list(language.scripts),
				"translationStatus": language.translation_status,
				"file": f"{language.code}.zip",
			}
		)
	result[root / "index.json"] = (
		json.dumps(catalog, ensure_ascii=False, indent=2) + "\n"
	).encode("utf-8")
	return result


def same_output(path: Path, expected: bytes) -> bool:
	if not path.exists():
		return False
	if path.suffix != ".zip":
		return path.read_bytes() == expected
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
