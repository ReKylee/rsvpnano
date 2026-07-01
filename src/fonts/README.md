# RSVP Nano U8g2 Fonts

This folder is intended for generated Arduino_GFX/U8g2 font headers.

## Noto Serif

Use `tools/generate_noto_serif_u8g2.py` to regenerate the Noto Serif U8g2 header from a local `NotoSerif-Regular.ttf` file.

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
