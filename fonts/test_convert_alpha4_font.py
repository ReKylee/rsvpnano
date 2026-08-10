import re
import struct
from pathlib import Path
from unittest import TestCase, mock

from fonts.convert_alpha4_font import (
    RFONT4_GLYPH_FORMAT,
    RFONT4_GLYPH_SIZE,
    RFONT4_HEADER_FORMAT,
    RFONT4_STRIKE_FORMAT,
    SCRIPT_MATH,
    capability_mask,
    mapped_codepoints,
    parse_locales,
    parse_scripts,
    script_mask,
)
from RSVPNanoCompanion.tools.generate_multilingual_corpus import PARAGRAPHS


class FontMapTest(TestCase):
    def test_auto_map_uses_only_the_fonts_unicode_cmap(self) -> None:
        with mock.patch("fonts.convert_alpha4_font.cmap_for_font", return_value={0x05D0: "alef", 0x05D1: "bet"}):
            self.assertEqual(mapped_codepoints(Path("font.ttf"), "auto"), [0x20, 0x3F, 0x05D0, 0x05D1])

    def test_reader_script_maps_keep_shared_text_but_exclude_other_scripts(self) -> None:
        cmap = {
            ord(" "): "space",
            ord("!"): "exclam",
            ord("0"): "zero",
            ord("?"): "question",
            ord("A"): "A",
            0x05B0: "sheva",
            0x05D0: "alef",
            0x060C: "comma-ar",
            0x0627: "alef-ar",
            0x064B: "fathatan",
            0x3042: "hiragana-a",
            0x30A2: "katakana-a",
            0x4E00: "one-han",
            0xFF0C: "comma-fullwidth",
            0x2200: "forall",
            0x1D465: "math-italic-x",
        }
        with mock.patch("fonts.convert_alpha4_font.cmap_for_font", return_value=cmap):
            self.assertEqual(
                {ord(" "), ord("!"), ord("0"), ord("?"), 0x05B0, 0x05D0},
                set(mapped_codepoints(Path("font.ttf"), "Hebr")),
            )
            self.assertEqual(
                {ord(" "), ord("!"), ord("0"), ord("?"), 0x060C, 0x0627, 0x064B},
                set(mapped_codepoints(Path("font.ttf"), "Arab")),
            )
            self.assertEqual(
                {ord(" "), ord("!"), ord("0"), ord("?"), 0x3042, 0x30A2, 0x4E00, 0xFF0C},
                set(mapped_codepoints(Path("font.ttf"), "Jpan")),
            )
            self.assertEqual(
                {ord(" "), ord("!"), ord("0"), ord("?"), 0x4E00, 0xFF0C},
                set(mapped_codepoints(Path("font.ttf"), "Hani")),
            )
            self.assertEqual(
                {ord(" "), ord("!"), ord("0"), ord("?"), 0x2200, 0x1D465},
                set(mapped_codepoints(Path("font.ttf"), "Zmth")),
            )

    def test_locale_affinity_is_compact_and_validated_offline(self) -> None:
        self.assertEqual(parse_locales("ja, zh-Hans"), ("ja", "zh-Hans"))
        with self.assertRaises(ValueError):
            parse_locales("ja,ja")

    def test_explicit_script_capabilities_do_not_depend_on_incidental_glyphs(self) -> None:
        self.assertEqual(parse_scripts("Zmth"), SCRIPT_MATH)
        self.assertEqual(capability_mask([0x57, 0x47, 0x57], SCRIPT_MATH), SCRIPT_MATH)
        self.assertEqual(script_mask(0xFFFFFFFF), 0)
        with self.assertRaises(ValueError):
            parse_scripts("Math")

    def test_generated_fonts_cover_the_multilingual_corpus(self) -> None:
        fonts = {
            "latin": alpha4_header_codepoints(Path("src/fonts/LiterataFallbackAlpha4.h")),
            "he": rfont4_codepoints(Path("fonts/Noto Serif Hebrew/font.rfont4")),
            "ar": rfont4_codepoints(Path("fonts/Noto Naskh Arabic/font.rfont4")),
            "ja": rfont4_codepoints(Path("fonts/Noto Serif Japanese/font.rfont4")),
            "zh-Hans": rfont4_codepoints(Path("fonts/Noto Serif Simplified Chinese/font.rfont4")),
            "math": rfont4_codepoints(Path("fonts/STIX Two Math/font.rfont4")),
        }
        builtin_ascii = set(range(0x20, 0x7F))
        for locale, _direction, text in PARAGRAPHS[:-1]:
            target = fonts.get(locale, fonts["latin"]) | builtin_ascii
            self.assertEqual(set(), {ord(char) for char in text if not char.isspace()} - target, locale)

        for locale, name in (("he", "Frank Ruhl Libre"), ("ar", "Amiri")):
            codepoints = rfont4_codepoints(Path(f"fonts/{name}/font.rfont4"))
            required = {
                ord(char)
                for language, _direction, text in PARAGRAPHS
                if language == locale
                for char in text
                if not char.isspace()
            }
            self.assertLessEqual(required, codepoints | builtin_ascii)
            self.assertTrue(set(range(ord("A"), ord("Z") + 1)).isdisjoint(codepoints), name)
            self.assertTrue(set(range(ord("a"), ord("z") + 1)).isdisjoint(codepoints), name)

        available = set().union(*fonts.values(), builtin_ascii)
        mixed = PARAGRAPHS[-1][2]
        self.assertEqual(set(), {ord(char) for char in mixed if not char.isspace()} - available)
        self.assertLessEqual({ord(char) for char in "∀∈ℝ²≥∫₀¹⅓"}, fonts["math"])

        for name in ("Noto Serif Japanese", "Noto Serif Simplified Chinese"):
            header = struct.unpack_from(RFONT4_HEADER_FORMAT, Path(f"fonts/{name}/font.rfont4").read_bytes())
            self.assertEqual(0, header[9], name)

        ascii_letters = set(range(ord("A"), ord("Z") + 1)) | set(range(ord("a"), ord("z") + 1))
        for name in (
            "Amiri",
            "Frank Ruhl Libre",
            "Noto Naskh Arabic",
            "Noto Serif Hebrew",
            "Noto Serif Japanese",
            "Noto Serif Simplified Chinese",
            "STIX Two Math",
        ):
            self.assertTrue(ascii_letters.isdisjoint(rfont4_codepoints(Path(f"fonts/{name}/font.rfont4"))), name)


def rfont4_codepoints(path: Path) -> set[int]:
    data = path.read_bytes()
    header = struct.unpack_from(RFONT4_HEADER_FORMAT, data)
    strike = struct.unpack_from(RFONT4_STRIKE_FORMAT, data, header[17])
    glyph_count, glyphs_offset = strike[0], strike[18]
    return {
        struct.unpack_from(RFONT4_GLYPH_FORMAT, data, glyphs_offset + index * RFONT4_GLYPH_SIZE)[0]
        for index in range(glyph_count)
    }


def alpha4_header_codepoints(path: Path) -> set[int]:
    glyphs = path.read_text(encoding="utf-8").split("Glyphs[] PROGMEM = {", 1)[1].split("};", 1)[0]
    return {int(value, 16) for value in re.findall(r"^\s*\{(0x[0-9A-F]+),", glyphs, re.MULTILINE)}
