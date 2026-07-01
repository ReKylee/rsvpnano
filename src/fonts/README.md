# RSVP Nano U8g2 Fonts

This folder is intended for generated Arduino_GFX/U8g2 font headers.

## Generic font conversion

Use `tools/convert_ttf_to_u8g2.sh` for any TrueType/OpenType font that should become a U8g2 C header.

```bash
SYMBOL=u8g2_font_rsvpnano_my_font_18_tf \
OUT=src/fonts/MyFontU8g2.h \
SIZE=18 \
MAP='32-126' \
tools/convert_ttf_to_u8g2.sh /path/to/MyFont-Regular.ttf
```

The generic script uses the normal U8g2 toolchain path:

```text
otf2bdf -> bdfconv -> U8g2 C header
```

Useful options:

- `SIZE`: pixel size passed to `otf2bdf`.
- `MAP`: U8g2/bdfconv glyph map, for example `32-126` or `32-126,8211,8212,8230`.
- `OUT`: generated header path.
- `SYMBOL`: exported U8g2 font symbol.
- `FONT_LABEL`: human-readable source font name for the generated header comment.
- `FONT_LICENSE_NOTE`: short license/attribution line for the generated header comment.
- `OTF2BDF` / `BDFCONV`: explicit tool paths when they are not on `PATH`.

## Noto Serif preset

Use `tools/convert_noto_serif_to_u8g2.sh` to regenerate the Noto Serif U8g2 header from a local `NotoSerif-Regular.ttf` file. It is a thin preset wrapper around the generic converter.

```bash
NOTO_SERIF_TTF=/path/to/NotoSerif-Regular.ttf \
  tools/convert_noto_serif_to_u8g2.sh
```

Default output:

```text
src/fonts/NotoSerifU8g2.h
```

Default exported symbol:

```cpp
u8g2_font_rsvpnano_noto_serif_18_tf
```

Default coverage is intentionally compact for firmware use:

- printable Basic Latin, `U+0020..U+007E`
- common reading punctuation: en dash, em dash, curly quotes, bullet, ellipsis

Noto Serif is licensed under the SIL Open Font License 1.1. Keep the generated attribution comment in the checked-in font header.
