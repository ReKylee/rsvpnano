# RSVP Nano fonts

This directory holds the offline font pipeline and the pre-converted `.rfont4` catalog used by the web flasher/companion.

Folder layout:

```text
fonts/
  convert_alpha4_font.py
  index.json
  Atkinson Hyperlegible/
    large.rfont4
    medium.rfont4
    small.rfont4
    OFL.txt
```

The generated fallback header is the one font artifact that belongs under `src` because it is compiled into firmware:

```text
src/fonts/
  LiterataFallbackAlpha4.h
```

`large`, `medium`, and `small` default to the reader sizes we already used in firmware: `52`, `43`, and `33` px. Override them with:

```bash
python fonts/convert_alpha4_font.py \
  --font path/to/MyFont.ttf \
  --name "My Font" \
  --sizes large=56,medium=44,small=34
```

Default output is a `.rfont4` folder under `fonts/<name>/` plus an updated `fonts/index.json`.

Generate the built-in fallback header only when firmware fallback data must change. By default this writes to `src/fonts/LiterataFallbackAlpha4.h`:

```bash
python fonts/convert_alpha4_font.py \
  --font fonts/Literata-VariableFont_opsz,wght.ttf \
  --name LiterataFallbackAlpha4 \
  --header
```

Runtime behavior:

- The SD card catalog is built from `/fonts/<folder name>/` directories.
- Each folder may contain `large.rfont4`, `medium.rfont4`, and/or `small.rfont4`.
- Missing sizes fall back to the built-in Literata fallback for that size.
- Only the currently selected `.rfont4` file is read into memory.
- Each bundled font keeps its license and any upstream font log beside its converted assets.
