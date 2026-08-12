import json
import sys
from pathlib import Path
from unittest import TestCase

from fonts.generate_fonts import CONVERTER, FONT_ROOT, PRESETS, converter_command, selected_presets


class FontPresetTest(TestCase):
    def test_default_presets_cover_the_catalog_and_builtin_fallback(self) -> None:
        catalog = json.loads((FONT_ROOT / "index.json").read_text(encoding="utf-8"))
        catalog_ids = {entry["id"] for entry in catalog}
        preset_ids = {preset.id for preset in PRESETS if not preset.header}

        self.assertEqual(catalog_ids, preset_ids)
        self.assertEqual(1, sum(preset.header for preset in PRESETS))
        self.assertTrue(all(preset.source.is_file() for preset in PRESETS))
        self.assertTrue(all((preset.source.parent / "OFL.txt").is_file() for preset in PRESETS))

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
        self.assertEqual("Arab", command[command.index("--map") + 1])
        self.assertNotIn("--sizes", command)
