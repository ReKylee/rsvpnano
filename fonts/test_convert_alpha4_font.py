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
)
from RSVPNanoCompanion.tools.generate_multilingual_corpus import PARAGRAPHS


class FontMapTest(TestCase):
    def test_auto_map_uses_only_the_fonts_unicode_cmap(self) -> None:
        with mock.patch("fonts.convert_alpha4_font.cmap_for_font", return_value={0x05D0: "alef", 0x05D1: "bet"}):
            self.assertEqual(mapped_codepoints(Path("font.ttf"), "auto"), [0x20, 0x3F, 0x05D0, 0x05D1])

    def test_locale_affinity_is_compact_and_validated_offline(self) -> None:
        self.assertEqual(parse_locales("ja, zh-Hans"), ("ja", "zh-Hans"))
        with self.assertRaises(ValueError):
            parse_locales("ja,ja")

    def test_explicit_script_capabilities_do_not_depend_on_incidental_glyphs(self) -> None:
        self.assertEqual(parse_scripts("Zmth"), SCRIPT_MATH)
        self.assertEqual(capability_mask([0x57, 0x47, 0x57], SCRIPT_MATH), SCRIPT_MATH)
        with self.assertRaises(ValueError):
            parse_scripts("Math")

    def test_generated_fonts_cover_the_multilingual_corpus(self) -> None:
        fonts = {
            "latin": rfont4_codepoints(Path("fonts/Literata/font.rfont4")),
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
            required = {
                ord(char)
                for language, _direction, text in PARAGRAPHS
                if language == locale
                for char in text
                if not char.isspace()
            }
            self.assertLessEqual(required, rfont4_codepoints(Path(f"fonts/{name}/font.rfont4")) | builtin_ascii)

        available = set().union(*fonts.values(), builtin_ascii)
        mixed = PARAGRAPHS[-1][2]
        self.assertEqual(set(), {ord(char) for char in mixed if not char.isspace()} - available)
        self.assertLessEqual({ord(char) for char in "∀∈ℝ²≥∫₀¹⅓"}, fonts["math"])

        for name in ("Noto Serif Japanese", "Noto Serif Simplified Chinese"):
            header = struct.unpack_from(RFONT4_HEADER_FORMAT, Path(f"fonts/{name}/font.rfont4").read_bytes())
            self.assertEqual(0, header[9], name)


def rfont4_codepoints(path: Path) -> set[int]:
    data = path.read_bytes()
    header = struct.unpack_from(RFONT4_HEADER_FORMAT, data)
    strike = struct.unpack_from(RFONT4_STRIKE_FORMAT, data, header[17])
    glyph_count, glyphs_offset = strike[0], strike[17]
    return {
        struct.unpack_from(RFONT4_GLYPH_FORMAT, data, glyphs_offset + index * RFONT4_GLYPH_SIZE)[0]
        for index in range(glyph_count)
    }
