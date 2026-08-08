# Locale packs

The `.zip` files are optional UI-localization overlays stamped with their generation time. Extract one at the SD-card
root so its contents land under `/locales/<locale>/`. Installing a locale pack changes only the interface language;
it is never required to read a book in that language.

`index.json` is the companion's online catalog. It points to these ZIP files; the companion extracts one locally and
streams its validated files through the existing staging-and-activation API, so the reader never needs to unzip it.

Reader capability belongs to RFont4. Installing one font under `/fonts/<family>/font.rfont4` adds the scripts and
OpenType shaping data declared in that file. Locale pack ZIPs never contain or refer to reader fonts.

`ui/font.u8g2` belongs to one locale pack and is declared by `[ui.font]` with generated codepoint
coverage. It is included only when that language's UI is not covered by the compiled font. Its absence explicitly
selects the compiled UI font.

The dependency split is intentionally one-way:

```text
UI locale pack -> UI strings + optional U8g2 font + UI direction
Reader font    -> RFont4 strikes + scripts/locales + optional OpenType tables
Both           -> shared Unicode, locale-tag, bidi, and shaping primitives
```

Neither installable root depends on the other. The reader's final missing-glyph fallback is the compiled U8g2 font,
not an installed locale asset.
