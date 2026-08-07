from __future__ import annotations

import hashlib
import io
import struct
import sys
import tomllib
import unittest
import zipfile
from dataclasses import replace
from datetime import datetime
from pathlib import Path
from tempfile import TemporaryDirectory

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

from generate_locale_packs import (  # noqa: E402
	DEFAULT_BUILTIN_UI_FONT,
	DEFAULT_OUTPUT,
	compiled_u8g2_font,
	outputs,
	u8g2_codepoints,
)
from generate_localization import DEFAULT_TOML, UiFont, load_model  # noqa: E402


class LocalePackTest(unittest.TestCase):
	def test_generated_archives_match_the_pack_contract(self) -> None:
		model = load_model(DEFAULT_TOML)
		for language in model.languages:
			if language.name == model.default_language:
				continue

			prefix = f"locales/{language.code}"
			with self.subTest(language=language.code), zipfile.ZipFile(
				DEFAULT_OUTPUT / f"{language.code}.zip"
			) as archive:
				self.assertEqual(
					archive.namelist(),
					[f"{prefix}/manifest.toml", f"{prefix}/ui/strings.bin"],
				)
				manifest = tomllib.loads(archive.read(f"{prefix}/manifest.toml").decode())
				strings = archive.read(f"{prefix}/ui/strings.bin")
				generated_at = datetime(*archive.infolist()[0].date_time).timestamp()
				self.assertTrue(
					all(
						info.date_time == archive.infolist()[0].date_time
						for info in archive.infolist()
					)
				)

			self.assertLessEqual(
				abs(
					(DEFAULT_OUTPUT / f"{language.code}.zip").stat().st_mtime
					- generated_at
				),
				5,
			)

			self.assertEqual(manifest["id"], language.code)
			self.assertEqual(manifest["schema_version"], 2)
			self.assertEqual(manifest["locale"], language.code)
			self.assertEqual(manifest["ui"]["strings"]["path"], "ui/strings.bin")
			self.assertEqual(manifest["ui"]["strings"]["bytes"], len(strings))
			self.assertEqual(
				manifest["ui"]["strings"]["sha256"], hashlib.sha256(strings).hexdigest()
			)
			self.assertNotIn("font", manifest["ui"])

			magic, version, count, text_bytes = struct.unpack_from("<4sHHI", strings)
			self.assertEqual(magic, b"RSL1")
			self.assertEqual(version, 1)
			self.assertEqual(count, len(model.texts))
			offsets = struct.unpack_from(f"<{count + 1}I", strings, 12)
			self.assertEqual(offsets[0], 0)
			self.assertEqual(offsets[-1], text_bytes)
			self.assertEqual(list(offsets), sorted(offsets))
			self.assertEqual(12 + 4 * len(offsets) + text_bytes, len(strings))

	def test_external_ui_font_is_validated_and_packaged(self) -> None:
		model = load_model(DEFAULT_TOML)
		font = compiled_u8g2_font(DEFAULT_BUILTIN_UI_FONT)
		with TemporaryDirectory() as directory:
			font_path = Path(directory) / "font.u8g2"
			font_path.write_bytes(font)
			language = replace(
				model.languages[1],
				ui_font=UiFont(source=str(font_path), license="Public domain"),
			)
			configured = replace(model, languages=[model.languages[0], language])
			generated = outputs(configured, Path(directory), (2026, 1, 2, 3, 4, 6))
			with zipfile.ZipFile(io.BytesIO(next(iter(generated.values())))) as archive:
				prefix = f"locales/{language.code}"
				self.assertIn(f"{prefix}/ui/font.u8g2", archive.namelist())
				manifest = tomllib.loads(archive.read(f"{prefix}/manifest.toml").decode())
				asset = manifest["ui"]["font"]
				self.assertEqual(asset["bytes"], len(font))
				self.assertEqual(asset["sha256"], hashlib.sha256(font).hexdigest())
				self.assertNotIn("codepoint_ranges", asset)

	def test_u8g2_coverage_comes_from_the_font_table(self) -> None:
		codepoints = u8g2_codepoints(compiled_u8g2_font(DEFAULT_BUILTIN_UI_FONT))
		self.assertIn(ord("A"), codepoints)
		self.assertIn(0x0411, codepoints)
		self.assertNotIn(0x4E00, codepoints)

	def test_locale_archives_never_contain_reader_fonts(self) -> None:
		model = load_model(DEFAULT_TOML)
		with TemporaryDirectory() as directory:
			generated = outputs(model, Path(directory), (2026, 1, 2, 3, 4, 6))
			for content in generated.values():
				with zipfile.ZipFile(io.BytesIO(content)) as archive:
					self.assertFalse(any(name.endswith(".rfont4") for name in archive.namelist()))

	def test_missing_ui_font_means_the_compiled_font_must_cover_the_language(self) -> None:
		model = load_model(DEFAULT_TOML)
		language = replace(model.languages[1], label="日本語", ui_font=None)
		configured = replace(model, languages=[model.languages[0], language])
		with TemporaryDirectory() as directory, self.assertRaisesRegex(ValueError, "needs ui_font"):
			outputs(configured, Path(directory), (2026, 1, 2, 3, 4, 6))

	def test_ui_engine_requirements_are_derived_from_script_metadata(self) -> None:
		model = load_model(DEFAULT_TOML)
		font = compiled_u8g2_font(DEFAULT_BUILTIN_UI_FONT)
		with TemporaryDirectory() as directory:
			font_path = Path(directory) / "font.u8g2"
			font_path.write_bytes(font)
			language = replace(
				model.languages[1],
				code="ar",
				scripts=("Arab",),
				direction="rtl",
				ui_font=UiFont(source=str(font_path), license="Public domain"),
			)
			configured = replace(model, languages=[model.languages[0], language])
			generated = outputs(configured, Path(directory), (2026, 1, 2, 3, 4, 6))
			with zipfile.ZipFile(io.BytesIO(next(iter(generated.values())))) as archive:
				manifest = tomllib.loads(archive.read("locales/ar/manifest.toml").decode())
				self.assertEqual(manifest["requires"], ["bidi", "shaping.opentype"])


if __name__ == "__main__":
	unittest.main()
