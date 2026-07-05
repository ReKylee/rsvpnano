# AlphaFont renderer

`AlphaFont` is the reader text path for generated Alpha4 anti-aliased fonts.

Runtime design:

- font bitmap data stays in `PROGMEM` as packed Alpha4 coverage
- final RGB565 pixels are always composed into mutable RAM buffers
- `draw16bitRGBBitmap(uint16_t*)` is used intentionally so Arduino_GFX selects the RAM bulk-transfer path
- no final RGB565 pixels are pushed directly from `PROGMEM`
- no `writePixel` / per-pixel display writes are used by the text renderer
- no heap allocation is required by the renderer

The generator moves most font-dependent work out of runtime:

- final Alpha4 mask creation after cutoff/gamma/despeckle
- glyph cropping using final non-zero Alpha4 pixels
- optional fallback-font glyph substitution for unsupported codepoints
- per-row packed Alpha4 storage: `rowStride = (width + 1) / 2`
- per-row visible span metadata, so runtime does not scan glyph rows
- Unicode page tables for O(1) glyph lookup by codepoint high/low byte
- per-left-glyph kerning slices

The current generator defaults are `--alpha-cutoff 32`, `--gamma 1.15`, and despeckle enabled.

The runtime still does only the work that depends on the actual word, color, screen position, and focus highlighting.
