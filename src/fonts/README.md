# RSVP Nano U8g2 Fonts

This folder is intended for generated Arduino_GFX/U8g2 font headers.

## Font conversion

Use `tools/convert_font_to_u8g2.py` for any TrueType, OpenType, TrueType Collection, or BDF font that should become a U8g2 C header.

The script uses U8g2's own tooling for the final font encoding:

```text
TTF/OTF/TTC --otf2bdf--> BDF --U8g2 bdfconv--> U8g2 C header
```

For BDF input, the first step is skipped:

```text
BDF --U8g2 bdfconv--> U8g2 C header
```

Example for Noto Serif:

```bash
python tools/convert_font_to_u8g2.py /path/to/NotoSerif-Regular.ttf \
  --out src/fonts/NotoSerifU8g2.h \
  --symbol u8g2_font_rsvpnano_noto_serif_18_tf \
  --font-label "Noto Serif Regular" \
  --license-note "Noto Serif is licensed under the SIL Open Font License 1.1."
```

By default, the script uses compact firmware-oriented coverage:

- printable Basic Latin, `U+0020..U+007E`
- common reading punctuation: en dash, em dash, curly quotes, bullet, ellipsis

Pass `--map` to change glyph coverage:

```bash
python tools/convert_font_to_u8g2.py /path/to/MyFont-Regular.ttf \
  --out src/fonts/MyFontU8g2.h \
  --symbol u8g2_font_rsvpnano_my_font_18_tf \
  --size 18 \
  --map '32-126'
```

Useful options:

- `--size`: pixel size passed to `otf2bdf` for TTF/OTF/TTC input.
- `--map`: U8g2/bdfconv glyph map, for example `32-126` or `32-126,8211,8212,8230`.
- `--out`: generated header path.
- `--symbol`: exported U8g2 font symbol.
- `--font-label`: human-readable source font name for the generated header comment.
- `--license-note`: short license/attribution line for the generated header comment.
- `--bdfconv`: explicit path to U8g2's `bdfconv` binary.
- `--u8g2-repo`: path to an upstream U8g2 checkout; the script looks for `tools/font/bdfconv/bdfconv` there.
- `--build-bdfconv`: run `make` in `<u8g2-repo>/tools/font/bdfconv` before conversion.
- `--otf2bdf`: explicit path to `otf2bdf` when it is not on `PATH`.

You can also set `BDFCONV`, `U8G2_REPO`, or `OTF2BDF` in the environment.

Keep license and attribution comments in checked-in generated font headers.
