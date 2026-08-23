# Reader-font bitmap locality maps

These generation-only maps place frequently used glyph bitmaps near the start of large RFont4 strikes.
They are not copied into RFont4 files or compiled into the firmware; runtime lookup tables and glyph record
order remain unchanged.

`ja.txt` and `zh-Hans.txt` contain unique Unicode codepoints in frequency-ranked, bounded co-occurrence clusters.
This keeps characters used in the same common words within fewer 4 KiB font blocks without pulling rare glyphs
arbitrarily far ahead of common independent characters.
`amiri-glyphs.txt` and `noto-naskh-arabic-glyphs.txt` contain font-specific glyph IDs produced by shaping the
Arabic word list. These contextual glyph IDs match the firmware's HarfBuzz 14.3.0 build.

The source words come from [wordfreq 3.1.1](https://github.com/rspeer/wordfreq) (Apache-2.0). The checked-in
maps contain only the resulting order, not source words or frequency values. Common punctuation is pinned first
because word tokenizers omit it.

Regenerate both maps from the repository root with:

```powershell
uv run --with wordfreq==3.1.1 --with uharfbuzz==0.56.0 python fonts/generate_locality_maps.py
```
