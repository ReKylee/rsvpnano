# RSVP Nano fonts

This directory holds the offline font pipeline and the pre-converted `.rfont4` catalog used by the web flasher/companion.

`RFont4` is named for its packed 4-bit alpha coverage. Format version 4 keeps that rendering format while using
32-bit Unicode codepoints, direct OpenType glyph IDs for shaping, and a generated script-capability mask. It stores
only packed glyph rows; the renderer finds visible spans after each row is read instead of storing duplicate indexes.
Older files must be regenerated.

Folder layout:

```text
fonts/
  convert_alpha4_font.py
  index.json
  Atkinson Hyperlegible/
    font.rfont4
    OFL.txt
```

The generated fallback header is the one font artifact that belongs under `src` because it is compiled into firmware:

```text
src/fonts/
  LiterataFallbackAlpha4.h
```

`large`, `medium`, and `small` default to the reader sizes we already used in firmware: `52`, `43`, and `33` px. Override them with:

```bash
uv run --with freetype-py --with fonttools python fonts/convert_alpha4_font.py \
  --font path/to/MyFont.ttf \
  --name "My Font" \
  --sizes large=56,medium=44,small=34
```

Add `--shaping` only for fonts whose scripts require contextual OpenType shaping. It embeds the subsetted
GDEF/GSUB/GPOS tables and glyph-ID closure once in the family file; ordinary Latin/Cyrillic fonts omit both.

Use `--locales` only as font-selection affinity; it does not install UI strings or make the font depend on a locale
pack. `--scripts` explicitly declares complete ISO 15924 capabilities and overrides cmap inference, preventing
incidental glyphs from advertising a whole script. Add `Zmth` only for a font intended to provide complete Unicode
math coverage rather than a font that happens to contain a few operators:

The named reader maps `Hebr`, `Arab`, `Hani`, `Jpan`, and `Zmth` select only their required script codepoints plus
reader punctuation, digits, currency symbols, and bidi controls. They also declare their script capabilities, so a
duplicate `--scripts` argument is unnecessary. Unrelated letters from multilingual source fonts are deliberately
omitted.

```bash
uv run --with freetype-py --with fonttools python fonts/convert_alpha4_font.py \
  --font path/to/NotoSerifHebrew-Regular.ttf \
  --name "Noto Serif Hebrew" \
  --map Hebr \
  --locales he \
  --shaping

uv run --with freetype-py --with fonttools python fonts/convert_alpha4_font.py \
  --font path/to/STIXTwoMath-Regular.otf \
  --name "STIX Two Math" \
  --map Zmth

uv run --with freetype-py --with fonttools python fonts/convert_alpha4_font.py \
  --font path/to/NotoSerifJP-Regular.otf \
  --name "Noto Serif Japanese" \
  --map Jpan \
  --locales ja
```

Horizontal CJK fonts intentionally omit `--shaping`; direct cmap glyphs cover this renderer, while vertical layout
is outside the current engine. Arabic and Hebrew retain their compact GDEF/GSUB/GPOS sections.

Default output is `fonts/<name>/font.rfont4` plus an updated `fonts/index.json`.

Generate the built-in fallback header only when firmware fallback data must change. By default this writes to `src/fonts/LiterataFallbackAlpha4.h`:

```bash
uv run --with freetype-py --with fonttools python fonts/convert_alpha4_font.py \
  --font .local/Literata/static/Literata-Regular.ttf \
  --name LiterataFallbackAlpha4 \
  --header
```

Runtime behavior:

- The SD card catalog is built from `/fonts/<folder name>/` directories.
- Each folder contains one `font.rfont4` family file with large, medium, and small raster strikes.
- Optional GDEF/GSUB/GPOS shaping tables are stored once in that same file, never in a locale pack.
- The selected family file stays open once; only each used strike's 256-byte Unicode page map is resident, while page indexes, glyph
  records, and Alpha4 rows are read through bounded renderer caches. Each glyph row needs one SD read.
- Each bundled font keeps its license and any upstream font log beside its converted assets.
- Generated RFont4 files include the rasterized glyph closure referenced by their shared layout tables, preserving
  the source font's OpenType glyph IDs.
- Locale-pack U8g2 fonts live under `/locales/<id>/ui` and are never loaded by the reader.
- If the selected RFont4 lacks a glyph, the renderer falls through to the compiled U8g2 font without loading another asset.
- Installing an RFont4 is sufficient to enable its reader scripts. Installing the matching UI locale is optional.

Validation is split deliberately: the converter and native tests verify complete generated assets, while firmware
checks the installed-file header and section layout before activation and bounds-checks each record it reads.
