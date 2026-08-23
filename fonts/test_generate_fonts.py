import json
import sys
from pathlib import Path
from unittest import TestCase

import uharfbuzz as hb

from fonts.convert_alpha4_font import read_hex_order
from fonts.generate_fonts import CONVERTER, FONT_ROOT, PRESETS, converter_command, selected_presets
from RSVPNanoCompanion.tools.generate_multilingual_corpus import PARAGRAPHS


class FontPresetTest(TestCase):
    def test_default_presets_cover_the_catalog_and_builtin_fallback(self) -> None:
        catalog = json.loads((FONT_ROOT / "index.json").read_text(encoding="utf-8"))
        catalog_ids = {entry["id"] for entry in catalog}
        preset_ids = {preset.id for preset in PRESETS if not preset.header}

        self.assertEqual(catalog_ids, preset_ids)
        self.assertTrue(all(entry["scripts"] for entry in catalog))
        self.assertTrue(all("scriptMask" not in entry for entry in catalog))
        self.assertEqual(1, sum(preset.header for preset in PRESETS))
        self.assertTrue(all(preset.source.is_file() for preset in PRESETS))
        self.assertTrue(all((preset.source.parent / "OFL.txt").is_file() for preset in PRESETS))
        self.assertTrue(all(preset.locality_map is None or preset.locality_map.is_file() for preset in PRESETS))
        self.assertTrue(
            all(preset.glyph_locality_map is None or preset.glyph_locality_map.is_file() for preset in PRESETS)
        )

        fallback = next(preset for preset in PRESETS if preset.header)
        self.assertEqual("Literata-Regular.ttf", fallback.source.name)

        localized = {preset.id: preset.locality_map for preset in PRESETS if preset.locality_map}
        self.assertEqual({"noto-serif-japanese", "noto-serif-simplified-chinese"}, set(localized))
        shaped = {preset.id: preset.glyph_locality_map for preset in PRESETS if preset.glyph_locality_map}
        self.assertEqual({"amiri", "noto-naskh-arabic"}, set(shaped))
        vertical = {preset.id for preset in PRESETS if preset.vertical}
        self.assertEqual({"noto-serif-japanese", "noto-serif-simplified-chinese"}, vertical)

    def test_shaped_locality_maps_cover_the_arabic_benchmark_text(self) -> None:
        text = next(text for locale, _direction, _chapter, text in PARAGRAPHS if locale == "ar") + " العربية"
        for preset in (preset for preset in PRESETS if preset.glyph_locality_map):
            buffer = hb.Buffer()
            buffer.add_str(text)
            buffer.direction = "rtl"
            buffer.script = "Arab"
            buffer.language = "ar"
            hb.shape(hb.Font(hb.Face(preset.source.read_bytes())), buffer)
            ranks = read_hex_order(preset.glyph_locality_map, 0xFFFF, "glyph ID")
            self.assertLessEqual({info.codepoint for info in buffer.glyph_infos if info.codepoint}, ranks.keys())

    def test_selection_keeps_canonical_generation_order(self) -> None:
        selected = selected_presets(["stix-two-math", "amiri"])
        self.assertEqual(["amiri", "stix-two-math"], [preset.id for preset in selected])
        with self.assertRaises(ValueError):
            selected_presets(["missing"])

    def test_batch_generator_delegates_to_the_single_font_cli(self) -> None:
        preset = next(preset for preset in PRESETS if preset.id == "amiri")
        command = converter_command(preset, Path("catalog"), Path("fallback.h"))

        self.assertEqual(sys.executable, command[0])
        self.assertEqual(str(CONVERTER), command[1])
        self.assertIn("--shaping", command)
        self.assertIn("--glyph-locality-map", command)
        self.assertEqual("Arab", command[command.index("--map") + 1])
        self.assertNotIn("--sizes", command)
