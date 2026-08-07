#!/usr/bin/env python3
"""Convert TrueType/OpenType fonts to sparse Alpha4 Arduino_GFX bitfonts.

This generator intentionally moves font-shape work out of the firmware runtime:

* FreeType rasterization, cutoff/gamma, and despeckling are done offline.
* Glyph boxes are cropped from the final Alpha4 mask, not raw coverage.
* Rows are packed independently as Alpha4, two pixels per byte.
* Renderers scan each packed row directly, avoiding duplicate row/span metadata.
* Unicode lookup uses generated 256-entry page tables for O(1) glyph lookup.
* GPOS kerning is emitted as per-left-glyph slices for small local lookups.
* Optional fallback font fills unsupported codepoints at generation time.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import struct
from dataclasses import dataclass, replace
from pathlib import Path

import freetype

try:
    from fontTools import subset as font_subset
    from fontTools.ttLib import TTFont
except Exception:  # pragma: no cover
    font_subset = None  # type: ignore[assignment]
    TTFont = None  # type: ignore[assignment]

DEFAULT_MAP = "32-126,160-383,512-591,1024-1279,8208-8230,8240,8249,8250,8364,8470"
DEFAULT_ALPHA_CUTOFF = 32
DEFAULT_GAMMA = 1.15
MISSING_GLYPH_INDEX = 0xFFFF
MISSING_PAGE_INDEX = 0xFF
SCRIPT_LATIN = 1 << 0
SCRIPT_CYRILLIC = 1 << 1
SCRIPT_GREEK = 1 << 2
SCRIPT_HEBREW = 1 << 3
SCRIPT_ARABIC = 1 << 4
SCRIPT_HAN = 1 << 5
SCRIPT_HIRAGANA = 1 << 6
SCRIPT_KATAKANA = 1 << 7
SCRIPT_HANGUL = 1 << 8
SCRIPT_MATH = 1 << 9

SCRIPT_TAGS = {
    "Latn": SCRIPT_LATIN,
    "Cyrl": SCRIPT_CYRILLIC,
    "Grek": SCRIPT_GREEK,
    "Hebr": SCRIPT_HEBREW,
    "Arab": SCRIPT_ARABIC,
    "Hani": SCRIPT_HAN,
    "Hira": SCRIPT_HIRAGANA,
    "Kana": SCRIPT_KATAKANA,
    "Hang": SCRIPT_HANGUL,
    "Zmth": SCRIPT_MATH,
}
SHAPED_GLYPH_CODEPOINT = 0xFFFFFFFF
FONT_LAYOUT_TAGS = ("GDEF", "GSUB", "GPOS")


@dataclass(frozen=True)
class CodepointRange:
    start: int
    end: int


@dataclass(frozen=True)
class GlyphRecord:
    codepoint: int
    glyph_id: int
    bitmap_offset: int
    kern_offset: int
    width: int
    height: int
    row_stride: int
    x_advance: int
    x_offset: int
    y_offset: int
    kern_count: int


@dataclass(frozen=True)
class RenderedGlyph:
    codepoint: int
    glyph_id: int
    rows: list[list[int]]
    width: int
    height: int
    row_stride: int
    x_advance: int
    x_offset: int
    y_offset: int


def c_identifier(name: str) -> str:
    ident = re.sub(r"[^0-9A-Za-z_]+", "_", name).strip("_")
    if not ident:
        ident = "font"
    if ident[0].isdigit():
        ident = "_" + ident
    return ident


def parse_codepoint_ranges(spec: str) -> list[CodepointRange]:
    ranges: list[CodepointRange] = []
    singles: set[int] = set()
    for token in spec.split(','):
        token = token.strip()
        if not token:
            continue
        if '-' in token:
            a, b = token.split('-', 1)
            start = int(a, 0)
            end = int(b, 0)
            if end < start:
                start, end = end, start
            ranges.append(CodepointRange(max(0, start), min(0x10FFFF, end)))
        else:
            value = int(token, 0)
            if 0 <= value <= 0x10FFFF:
                singles.add(value)

    singles.update((ord('?'), ord(' ')))
    ranges.extend(CodepointRange(value, value) for value in singles)
    ranges = sorted(ranges, key=lambda item: (item.start, item.end))

    merged: list[CodepointRange] = []
    for item in ranges:
        if not merged or item.start > merged[-1].end + 1:
            merged.append(item)
        else:
            merged[-1] = CodepointRange(merged[-1].start, max(merged[-1].end, item.end))
    return merged


def script_mask(codepoint: int) -> int:
    if (ord('A') <= codepoint <= ord('Z') or ord('a') <= codepoint <= ord('z')
            or codepoint in (0x00AA, 0x00B5, 0x00BA)
            or 0x00C0 <= codepoint <= 0x00D6 or 0x00D8 <= codepoint <= 0x00F6
            or 0x00F8 <= codepoint <= 0x024F or 0x1E00 <= codepoint <= 0x1EFF):
        return SCRIPT_LATIN
    if (0x0400 <= codepoint <= 0x052F or 0x2DE0 <= codepoint <= 0x2DFF
            or 0xA640 <= codepoint <= 0xA69F):
        return SCRIPT_CYRILLIC
    if 0x0370 <= codepoint <= 0x03FF or 0x1F00 <= codepoint <= 0x1FFF:
        return SCRIPT_GREEK
    if 0x0590 <= codepoint <= 0x05FF or 0xFB1D <= codepoint <= 0xFB4F:
        return SCRIPT_HEBREW
    if (0x0600 <= codepoint <= 0x06FF or 0x0750 <= codepoint <= 0x077F
            or 0x08A0 <= codepoint <= 0x08FF or 0xFB50 <= codepoint <= 0xFDFF
            or 0xFE70 <= codepoint <= 0xFEFF):
        return SCRIPT_ARABIC
    if (0x3400 <= codepoint <= 0x4DBF or 0x4E00 <= codepoint <= 0x9FFF
            or 0xF900 <= codepoint <= 0xFAFF or 0x20000 <= codepoint <= 0x323AF):
        return SCRIPT_HAN
    if 0x3040 <= codepoint <= 0x309F:
        return SCRIPT_HIRAGANA
    if 0x30A0 <= codepoint <= 0x30FF or 0x31F0 <= codepoint <= 0x31FF or 0xFF66 <= codepoint <= 0xFF9F:
        return SCRIPT_KATAKANA
    if (0x1100 <= codepoint <= 0x11FF or 0x3130 <= codepoint <= 0x318F
            or 0xA960 <= codepoint <= 0xA97F or 0xAC00 <= codepoint <= 0xD7FF):
        return SCRIPT_HANGUL
    return 0


def codepoints_from_ranges(ranges: list[CodepointRange]) -> list[int]:
    return [cp for item in ranges for cp in range(item.start, item.end + 1)]


def mapped_codepoints(font_path: Path, spec: str) -> list[int]:
    if spec != "auto":
        return codepoints_from_ranges(parse_codepoint_ranges(spec))
    return sorted(set(cmap_for_font(font_path)) | {ord('?'), ord(' ')})


def capability_mask(strike_masks: list[int], declared: int) -> int:
    if declared:
        return declared
    result = strike_masks[0]
    for mask in strike_masks[1:]:
        result &= mask
    return result


def row_stride_bytes(width: int) -> int:
    return (width + 1) // 2


def quantize4(value: int, cutoff: int, gamma: float) -> int:
    if value <= cutoff:
        return 0
    value = max(0, min(255, value))
    normalized = value / 255.0
    if gamma > 0.0 and gamma != 1.0:
        normalized = normalized ** gamma
    return max(0, min(15, int(normalized * 15.0 + 0.5)))


def bitmap_alpha_at(buffer: bytes, pitch: int, x: int, y: int) -> int:
    if pitch >= 0:
        return buffer[y * pitch + x]
    return buffer[(y + 1) * pitch + x]


def despeckle_alpha4(rows: list[list[int]], weak_threshold: int, strong_threshold: int, max_neighbors: int) -> list[list[int]]:
    if not rows or not rows[0]:
        return rows
    height = len(rows)
    width = len(rows[0])
    out = [row[:] for row in rows]

    def at(x: int, y: int) -> int:
        if x < 0 or y < 0 or x >= width or y >= height:
            return 0
        return rows[y][x]

    for y in range(height):
        for x in range(width):
            value = rows[y][x]
            if value == 0 or value > weak_threshold:
                continue

            nonzero_neighbors = 0
            strong_neighbor = False
            for yy in range(y - 1, y + 2):
                for xx in range(x - 1, x + 2):
                    if xx == x and yy == y:
                        continue
                    neighbor = at(xx, yy)
                    if neighbor != 0:
                        nonzero_neighbors += 1
                    if neighbor >= strong_threshold:
                        strong_neighbor = True

            if not strong_neighbor and nonzero_neighbors <= max_neighbors:
                out[y][x] = 0

    return out


def crop_rows(rows: list[list[int]]) -> tuple[list[list[int]], int, int]:
    if not rows or not rows[0]:
        return [], 0, 0
    height = len(rows)
    width = len(rows[0])
    min_x = width
    min_y = height
    max_x = -1
    max_y = -1
    for y, row in enumerate(rows):
        for x, value in enumerate(row):
            if value != 0:
                min_x = min(min_x, x)
                min_y = min(min_y, y)
                max_x = max(max_x, x)
                max_y = max(max_y, y)
    if max_x < min_x or max_y < min_y:
        return [], 0, 0
    cropped = [rows[y][min_x:max_x + 1] for y in range(min_y, max_y + 1)]
    return cropped, min_x, min_y


def pack_alpha4_rows(rows: list[list[int]], width: int, height: int) -> bytes:
    out = bytearray()
    for y in range(height):
        row = rows[y]
        for x in range(0, width, 2):
            a0 = row[x] & 0x0F
            a1 = (row[x + 1] & 0x0F) if x + 1 < width else 0
            out.append((a0 << 4) | a1)
    return bytes(out)


def render_glyph_id(
    face: freetype.Face,
    codepoint: int,
    glyph_id: int,
    cutoff: int,
    gamma: float,
    enable_despeckle: bool,
    weak_threshold: int,
    strong_threshold: int,
    max_neighbors: int,
) -> RenderedGlyph | None:
    try:
        face.load_glyph(glyph_id, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_NORMAL)
    except freetype.ft_errors.FT_Exception:
        return None

    glyph = face.glyph
    bitmap = glyph.bitmap
    src_w = int(bitmap.width)
    src_h = int(bitmap.rows)
    pitch = int(bitmap.pitch)
    buffer = bytes(bitmap.buffer)

    x_advance = max(0, min(255, int(round(glyph.advance.x / 64.0))))
    x_offset = max(-128, min(127, int(glyph.bitmap_left)))
    y_offset = max(-128, min(127, -int(glyph.bitmap_top)))

    if src_w <= 0 or src_h <= 0:
        return RenderedGlyph(codepoint, glyph_id, [], 0, 0, 0, x_advance, 0, 0)

    alpha4_rows: list[list[int]] = []
    for y in range(src_h):
        row: list[int] = []
        for x in range(src_w):
            alpha = bitmap_alpha_at(buffer, pitch, x, y)
            row.append(quantize4(alpha, cutoff, gamma))
        alpha4_rows.append(row)

    if enable_despeckle:
        alpha4_rows = despeckle_alpha4(alpha4_rows, weak_threshold, strong_threshold, max_neighbors)

    cropped, min_x, min_y = crop_rows(alpha4_rows)
    if not cropped:
        return RenderedGlyph(codepoint, glyph_id, [], 0, 0, 0, x_advance, 0, 0)

    height = min(255, len(cropped))
    width = min(255, len(cropped[0]))
    cropped = [row[:width] for row in cropped[:height]]

    return RenderedGlyph(
        codepoint=codepoint,
        glyph_id=glyph_id,
        rows=cropped,
        width=width,
        height=height,
        row_stride=row_stride_bytes(width),
        x_advance=x_advance,
        x_offset=max(-128, min(127, x_offset + min_x)),
        y_offset=max(-128, min(127, y_offset + min_y)),
    )


def render_glyph(
    face: freetype.Face,
    codepoint: int,
    cutoff: int,
    gamma: float,
    enable_despeckle: bool,
    weak_threshold: int,
    strong_threshold: int,
    max_neighbors: int,
) -> RenderedGlyph | None:
    glyph_id = int(face.get_char_index(codepoint))
    if glyph_id == 0 and codepoint not in (ord(' '), ord('?')):
        return None
    return render_glyph_id(
        face, codepoint, glyph_id, cutoff, gamma, enable_despeckle,
        weak_threshold, strong_threshold, max_neighbors,
    )


def emit_bytes(values: bytes | list[int], indent: str = "    ") -> str:
    if isinstance(values, list):
        values = bytes(values)
    lines: list[str] = []
    for i in range(0, len(values), 16):
        chunk = values[i:i + 16]
        lines.append(indent + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    return "\n".join(lines)


def emit_uint16(values: list[int], indent: str = "    ") -> str:
    lines: list[str] = []
    for i in range(0, len(values), 12):
        chunk = values[i:i + 12]
        lines.append(indent + ", ".join(f"0x{value:04X}" for value in chunk) + ",")
    return "\n".join(lines)


def cmap_for_font(font_path: Path) -> dict[int, str]:
    if TTFont is None:
        return {}
    font = TTFont(str(font_path))
    cmap: dict[int, str] = {}
    for table in font["cmap"].tables:
        if table.isUnicode():
            cmap.update(table.cmap)
    font.close()
    return cmap


def font_layout(font_path: Path, codepoints: list[int]) -> tuple[int, int, dict[str, bytes], set[int]]:
    if TTFont is None or font_subset is None:
        raise RuntimeError("fontTools is required to generate reader fonts")

    font = TTFont(str(font_path))
    glyph_count = len(font.getGlyphOrder())
    units_per_em = int(font["head"].unitsPerEm)
    options = font_subset.Options()
    options.retain_gids = True
    options.layout_features = ["*"]
    subsetter = font_subset.Subsetter(options=options)
    subsetter.populate(unicodes=codepoints)
    subsetter.subset(font)
    glyph_ids = {subsetter.reverseOrigGlyphMap[name] for name in subsetter.glyphs_gsubed}
    tables = {tag: font.getTableData(tag) for tag in FONT_LAYOUT_TAGS if tag in font}
    font.close()
    return units_per_em, glyph_count, tables, glyph_ids


def value_x_advance(value_record: object | None) -> int:
    if value_record is None:
        return 0
    return int(getattr(value_record, "XAdvance", 0) or 0)


def gpos_kerning_units(font_path: Path, codepoints: list[int]) -> dict[tuple[int, int], int]:
    if TTFont is None:
        return {}

    font = TTFont(str(font_path))
    if "GPOS" not in font:
        font.close()
        return {}

    cmap: dict[int, str] = {}
    for table in font["cmap"].tables:
        if table.isUnicode():
            cmap.update(table.cmap)

    glyph_to_codepoints: dict[str, list[int]] = {}
    for codepoint in codepoints:
        glyph_name = cmap.get(codepoint)
        if glyph_name is not None:
            glyph_to_codepoints.setdefault(glyph_name, []).append(codepoint)

    glyphs = set(glyph_to_codepoints)
    pairs: dict[tuple[int, int], int] = {}
    for lookup in font["GPOS"].table.LookupList.Lookup:
        if lookup.LookupType != 2:
            continue
        for subtable in lookup.SubTable:
            coverage = set(subtable.Coverage.glyphs)
            if subtable.Format == 1:
                for first_glyph, pair_set in zip(subtable.Coverage.glyphs, subtable.PairSet):
                    if first_glyph not in glyphs:
                        continue
                    for pair_value in pair_set.PairValueRecord:
                        second_glyph = pair_value.SecondGlyph
                        if second_glyph not in glyphs:
                            continue
                        adjust = value_x_advance(pair_value.Value1)
                        if adjust == 0:
                            continue
                        for left in glyph_to_codepoints[first_glyph]:
                            for right in glyph_to_codepoints[second_glyph]:
                                pairs[(left, right)] = pairs.get((left, right), 0) + adjust
            elif subtable.Format == 2:
                class1 = {glyph: subtable.ClassDef1.classDefs.get(glyph, 0) for glyph in glyphs}
                class2 = {glyph: subtable.ClassDef2.classDefs.get(glyph, 0) for glyph in glyphs}
                glyphs_by_class2: dict[int, list[str]] = {}
                for glyph, klass in class2.items():
                    glyphs_by_class2.setdefault(klass, []).append(glyph)

                for first_glyph in glyphs:
                    if first_glyph not in coverage:
                        continue
                    class1_id = class1.get(first_glyph, 0)
                    if class1_id >= len(subtable.Class1Record):
                        continue
                    class2_records = subtable.Class1Record[class1_id].Class2Record
                    for class2_id, class2_record in enumerate(class2_records):
                        adjust = value_x_advance(class2_record.Value1)
                        if adjust == 0:
                            continue
                        for second_glyph in glyphs_by_class2.get(class2_id, []):
                            for left in glyph_to_codepoints[first_glyph]:
                                for right in glyph_to_codepoints[second_glyph]:
                                    pairs[(left, right)] = pairs.get((left, right), 0) + adjust

    font.close()
    return pairs


def units_per_em(font_path: Path) -> int:
    if TTFont is None:
        return 0
    font = TTFont(str(font_path))
    value = int(font["head"].unitsPerEm)
    font.close()
    return value


def build_page_tables(glyph_index_by_codepoint: dict[int, int]) -> tuple[list[int], list[tuple[int, list[int]]]]:
    pages_by_high: dict[int, list[int]] = {}
    indexed = {cp: index for cp, index in glyph_index_by_codepoint.items() if cp <= 0xFFFF and index < 0xFFFF}
    for high in sorted({cp >> 8 for cp in indexed}):
        table = [MISSING_GLYPH_INDEX] * 256
        for codepoint, glyph_index in indexed.items():
            if (codepoint >> 8) == high:
                table[codepoint & 0xFF] = glyph_index
        pages_by_high[high] = table

    if len(pages_by_high) > MISSING_PAGE_INDEX:
        raise ValueError("RFont4 supports at most 255 indexed BMP pages")

    page_map = [MISSING_PAGE_INDEX] * 256
    pages: list[tuple[int, list[int]]] = []
    for index, high in enumerate(sorted(pages_by_high)):
        page_map[high] = index
        pages.append((high, pages_by_high[high]))
    return page_map, pages


def word_metric_codepoints() -> list[int]:
    # Main RSVP guide metrics should follow the selected font, not the current word.
    # These glyphs cover typical ascenders, x-height, descenders, caps, and digits.
    return [*range(ord('0'), ord('9') + 1), *range(ord('A'), ord('Z') + 1), *range(ord('a'), ord('z') + 1)]


def fallback_word_metric_codepoints() -> list[int]:
    # Offline fallback only. This mirrors the runtime reference string that looked visually good,
    # but the generated AlphaFont still always contains a concrete metric.
    return [ord(ch) for ch in "Hgj"]


def ink_bounds_for_codepoints(records: list[GlyphRecord], codepoints: list[int]) -> tuple[int, int] | None:
    wanted = set(codepoints)
    top = 127
    bottom = -128

    for record in records:
        if record.codepoint not in wanted or record.width == 0 or record.height == 0:
            continue
        glyph_top = record.y_offset
        glyph_bottom = record.y_offset + record.height - 1
        top = min(top, glyph_top)
        bottom = max(bottom, glyph_bottom)

    if top > bottom:
        return None
    return max(-128, min(127, top)), max(-128, min(127, bottom))


def word_ink_metrics(records: list[GlyphRecord]) -> tuple[int, int]:
    return (
        ink_bounds_for_codepoints(records, word_metric_codepoints())
        or ink_bounds_for_codepoints(records, fallback_word_metric_codepoints())
        or (0, -1)
    )


def generate_font(
    font_path: Path,
    fallback_font_path: Path | None,
    size: int,
    symbol: str,
    codepoints: list[int],
    shaping_glyph_ids: set[int],
    kerning_units: dict[tuple[int, int], int],
    upm: int,
    cutoff: int,
    gamma: float,
    enable_despeckle: bool,
    weak_threshold: int,
    strong_threshold: int,
    max_neighbors: int,
) -> tuple[str, dict[str, int | float]]:
    face = freetype.Face(str(font_path))
    face.set_char_size(size * 64, size * 64, 72, 72)

    fallback_face: freetype.Face | None = None
    if fallback_font_path is not None:
        fallback_face = freetype.Face(str(fallback_font_path))
        fallback_face.set_char_size(size * 64, size * 64, 72, 72)

    bitmap = bytearray()
    records: list[GlyphRecord] = []
    glyph_index_by_codepoint: dict[int, int] = {}
    rendered_glyphs: list[RenderedGlyph] = []
    missing = 0
    fallback_glyphs = 0

    for codepoint in codepoints:
        rendered = render_glyph(face, codepoint, cutoff, gamma, enable_despeckle, weak_threshold, strong_threshold, max_neighbors)
        if rendered is None and fallback_face is not None:
            rendered = render_glyph(fallback_face, codepoint, cutoff, gamma, enable_despeckle, weak_threshold, strong_threshold, max_neighbors)
            if rendered is not None:
                # Shaping glyph IDs belong to the primary face, never its raster fallback.
                rendered = replace(rendered, glyph_id=0)
                fallback_glyphs += 1
        if rendered is None:
            missing += 1
            continue
        rendered_glyphs.append(rendered)
        glyph_index_by_codepoint[codepoint] = len(records)
        records.append(GlyphRecord(
            codepoint=codepoint,
            glyph_id=rendered.glyph_id,
            bitmap_offset=0,
            kern_offset=0,
            width=rendered.width,
            height=rendered.height,
            row_stride=rendered.row_stride,
            x_advance=rendered.x_advance,
            x_offset=rendered.x_offset,
            y_offset=rendered.y_offset,
            kern_count=0,
        ))

    direct_glyph_ids = {rendered.glyph_id for rendered in rendered_glyphs}
    for glyph_id in sorted(shaping_glyph_ids - direct_glyph_ids - {0}):
        rendered = render_glyph_id(
            face, SHAPED_GLYPH_CODEPOINT, glyph_id, cutoff, gamma, enable_despeckle,
            weak_threshold, strong_threshold, max_neighbors,
        )
        if rendered is None:
            raise ValueError(f"could not render shaping glyph {glyph_id}")
        rendered_glyphs.append(rendered)
        records.append(GlyphRecord(
            codepoint=SHAPED_GLYPH_CODEPOINT,
            glyph_id=glyph_id,
            bitmap_offset=0,
            kern_offset=0,
            width=rendered.width,
            height=rendered.height,
            row_stride=rendered.row_stride,
            x_advance=rendered.x_advance,
            x_offset=rendered.x_offset,
            y_offset=rendered.y_offset,
            kern_count=0,
        ))

    # Runtime-friendly kerning slices: all pairs for a left glyph are contiguous and sorted by right codepoint.
    pairs_by_left: dict[int, list[tuple[int, int]]] = {cp: [] for cp in glyph_index_by_codepoint}
    if upm > 0:
        for (left, right), value_units in kerning_units.items():
            if left not in glyph_index_by_codepoint or right not in glyph_index_by_codepoint:
                continue
            pixels = int(round((value_units * size) / upm))
            if pixels != 0:
                pairs_by_left.setdefault(left, []).append((right, max(-128, min(127, pixels))))

    kern_pairs_flat: list[tuple[int, int]] = []

    final_records: list[GlyphRecord] = []
    for record, rendered in zip(records, rendered_glyphs):
        bitmap_offset = len(bitmap)
        if rendered.width > 0 and rendered.height > 0:
            bitmap.extend(pack_alpha4_rows(rendered.rows, rendered.width, rendered.height))

        unique_pairs = sorted(set(pairs_by_left.get(record.codepoint, [])), key=lambda item: item[0])
        kern_offset = len(kern_pairs_flat)
        kern_pairs_flat.extend(unique_pairs)

        final_records.append(GlyphRecord(
            codepoint=record.codepoint,
            glyph_id=record.glyph_id,
            bitmap_offset=bitmap_offset,
            kern_offset=kern_offset,
            width=record.width,
            height=record.height,
            row_stride=record.row_stride,
            x_advance=record.x_advance,
            x_offset=record.x_offset,
            y_offset=record.y_offset,
            kern_count=len(unique_pairs),
        ))

    records = final_records
    generated_script_mask = 0
    for record in records:
        generated_script_mask |= script_mask(record.codepoint)
    page_map, pages = build_page_tables(glyph_index_by_codepoint)
    glyph_index_by_id: dict[int, int] = {}
    for index, record in enumerate(records):
        if record.glyph_id != 0:
            glyph_index_by_id.setdefault(record.glyph_id, index)
    glyph_ids = sorted(glyph_index_by_id.items())

    ascent = max(0, min(255, int(math.ceil(face.size.ascender / 64.0))))
    descent = max(0, min(255, int(math.ceil(abs(face.size.descender / 64.0)))))
    y_advance = max(1, min(255, int(math.ceil(face.size.height / 64.0))))
    word_ink_top, word_ink_bottom = word_ink_metrics(records)

    bitmap_name = f"{symbol}Bitmap"
    glyph_name = f"{symbol}Glyphs"
    page_map_name = f"{symbol}PageMap"
    pages_name = f"{symbol}Pages"
    kern_name = f"{symbol}Kerning"
    glyph_ids_name = f"{symbol}GlyphIds"

    lines: list[str] = []
    lines.append(f"constexpr uint8_t {symbol}MaxGlyphWidth = {max((rec.width for rec in records), default=0)};")
    lines.append(f"constexpr uint8_t {symbol}MaxGlyphHeight = {max((rec.height for rec in records), default=0)};")
    lines.append("")

    lines.append(f"inline constexpr uint8_t {bitmap_name}[] PROGMEM = {{")
    if bitmap:
        lines.append(emit_bytes(bytes(bitmap)))
    lines.append("};")
    lines.append("")

    lines.append(f"inline constexpr AlphaKerningPair {kern_name}[] PROGMEM = {{")
    for right, pixels in kern_pairs_flat:
        lines.append(f"    {{0x{right:04X}, {pixels}}},")
    lines.append("};")
    lines.append("")

    lines.append(f"inline constexpr AlphaGlyph {glyph_name}[] PROGMEM = {{")
    for rec in records:
        lines.append(
            f"    {{0x{rec.codepoint:04X}, {rec.bitmap_offset}UL, {rec.kern_offset}, "
            f"{rec.width}, {rec.height}, {rec.row_stride}, {rec.x_advance}, "
            f"{rec.x_offset}, {rec.y_offset}, {rec.kern_count}, {rec.glyph_id}UL}},"
        )
    lines.append("};")
    lines.append("")

    lines.append(f"inline constexpr AlphaGlyphId {glyph_ids_name}[] PROGMEM = {{")
    for glyph_id, glyph_index in glyph_ids:
        lines.append(f"    {{{glyph_id}UL, {glyph_index}UL}},")
    lines.append("};")
    lines.append("")

    lines.append(f"inline constexpr uint8_t {page_map_name}[] PROGMEM = {{")
    lines.append(emit_bytes(page_map))
    lines.append("};")
    lines.append("")

    page_symbols: list[str] = []
    for index, (high, table) in enumerate(pages):
        page_symbol = f"{symbol}Page{index:02d}"
        page_symbols.append(page_symbol)
        lines.append(f"// U+{high:02X}xx")
        lines.append(f"inline constexpr uint16_t {page_symbol}[] PROGMEM = {{")
        lines.append(emit_uint16(table))
        lines.append("};")
        lines.append("")

    lines.append(f"inline constexpr const uint16_t* {pages_name}[] PROGMEM = {{")
    for page_symbol in page_symbols:
        lines.append(f"    {page_symbol},")
    lines.append("};")
    lines.append("")

    lines.append(f"inline constexpr AlphaFont {symbol} = {{")
    lines.append(f"    \"{symbol}\",")
    lines.append(f"    {bitmap_name},")
    lines.append(f"    {glyph_name},")
    lines.append(f"    {len(records)},")
    lines.append(f"    {y_advance},")
    lines.append(f"    {ascent},")
    lines.append(f"    {descent},")
    lines.append(f"    {page_map_name},")
    lines.append(f"    {pages_name},")
    lines.append(f"    {len(pages)},")
    lines.append(f"    {kern_name},")
    lines.append(f"    {len(kern_pairs_flat)},")
    lines.append(f"    {word_ink_top},")
    lines.append(f"    {word_ink_bottom},")
    lines.append(f"    {glyph_ids_name},")
    lines.append(f"    {len(glyph_ids)},")
    lines.append(f"    0x{generated_script_mask:08X}UL,")
    lines.append(f"    {size},")
    lines.append("};")
    lines.append("")

    raw_alpha4_bytes = sum(row_stride_bytes(rec.width) * rec.height for rec in records)
    raw_alpha8_bytes = sum(rec.width * rec.height for rec in records)
    stats: dict[str, int | float] = {
        "size": size,
        "glyphs": len(records),
        "missing": missing,
        "fallback_glyphs": fallback_glyphs,
        "bitmap_bytes": len(bitmap),
        "page_count": len(pages),
        "raw_alpha4_bytes": raw_alpha4_bytes,
        "raw_alpha8_bytes": raw_alpha8_bytes,
        "ascent": ascent,
        "descent": descent,
        "y_advance": y_advance,
        "word_ink_top": word_ink_top,
        "word_ink_bottom": word_ink_bottom,
        "cutoff": cutoff,
        "gamma": gamma,
        "kerning_pairs": len(kern_pairs_flat),
        "glyph_ids": len(glyph_ids),
        "shaping_glyphs": len(shaping_glyph_ids - direct_glyph_ids - {0}),
        "script_mask": generated_script_mask,
    }
    return "\n".join(lines), stats


SIZE_LABELS = ("large", "medium", "small")
RFONT4_MAGIC = 0x34544652
RFONT4_VERSION = 4
RFONT4_HEADER_FORMAT = "<I7H2B3H7I"
RFONT4_STRIKE_FORMAT = "<IIIHBBBbbBBBH9I"
RFONT4_GLYPH_FORMAT = "<III4BbbHI"
RFONT4_KERN_FORMAT = "<Ib"
RFONT4_GLYPH_ID_FORMAT = "<II"
RFONT4_LAYOUT_TABLE_FORMAT = "<III"
RFONT4_HEADER_SIZE = struct.calcsize(RFONT4_HEADER_FORMAT)
RFONT4_STRIKE_SIZE = struct.calcsize(RFONT4_STRIKE_FORMAT)
RFONT4_GLYPH_SIZE = struct.calcsize(RFONT4_GLYPH_FORMAT)
RFONT4_KERN_SIZE = struct.calcsize(RFONT4_KERN_FORMAT)
RFONT4_GLYPH_ID_SIZE = struct.calcsize(RFONT4_GLYPH_ID_FORMAT)
RFONT4_LAYOUT_TABLE_SIZE = struct.calcsize(RFONT4_LAYOUT_TABLE_FORMAT)


def normalize_catalog_id(name: str) -> str:
    ident = re.sub(r"[^0-9A-Za-z]+", "-", name).strip("-").lower()
    return ident or "font"


def parse_locales(spec: str) -> tuple[str, ...]:
    locales = tuple(locale.strip() for locale in spec.split(',') if locale.strip())
    if len(set(locales)) != len(locales) or any(
        not re.fullmatch(r"[A-Za-z]{2,8}(?:-[A-Za-z0-9]{1,8})*", locale) for locale in locales
    ):
        raise ValueError("--locales must contain unique BCP 47 tags")
    return locales


def parse_scripts(spec: str) -> int:
    tags = tuple(tag.strip() for tag in spec.split(',') if tag.strip())
    if len(set(tags)) != len(tags) or any(tag not in SCRIPT_TAGS for tag in tags):
        raise ValueError(f"--scripts must contain unique tags from {', '.join(SCRIPT_TAGS)}")
    return sum(SCRIPT_TAGS[tag] for tag in tags)


def parse_size_spec(spec: str) -> list[tuple[str, int]]:
    tokens = [token.strip() for token in spec.split(',') if token.strip()]
    parsed: list[tuple[str, int]] = []
    if any('=' in token for token in tokens):
        by_name: dict[str, int] = {}
        for token in tokens:
            label, value = token.split('=', 1)
            label = label.strip().lower()
            if label not in SIZE_LABELS:
                raise ValueError(f"unknown size label '{label}', expected large/medium/small")
            by_name[label] = int(value.strip(), 0)
        for label in SIZE_LABELS:
            if label not in by_name:
                raise ValueError(f"missing {label}=... in --sizes")
            parsed.append((label, by_name[label]))
        return parsed
    values = [int(token, 0) for token in tokens]
    if len(values) != len(SIZE_LABELS):
        raise ValueError("--sizes must be either large=52,medium=43,small=33 or three comma-separated values")
    return list(zip(SIZE_LABELS, values))


def strip_int_suffix(value: str) -> str:
    return value.strip().removesuffix('UL').removesuffix('U').removesuffix('L')


def parse_int_literal(value: str) -> int:
    return int(strip_int_suffix(value), 0)


def array_block(body: str, array_name: str) -> str:
    pattern = re.compile(rf"inline constexpr [^=]+\b{re.escape(array_name)}\[\][^=]*= \{{(.*?)\}};", re.S)
    match = pattern.search(body)
    if not match:
        raise ValueError(f"could not find generated array {array_name}")
    return match.group(1)


def parse_byte_array(body: str, array_name: str) -> bytes:
    return bytes(int(item, 16) for item in re.findall(r"0x([0-9A-Fa-f]{2})", array_block(body, array_name)))


def parse_uint16_array_block(block: str) -> list[int]:
    return [int(item, 16) for item in re.findall(r"0x([0-9A-Fa-f]{4})", block)]


def parse_record_entries(block: str) -> list[list[int]]:
    entries: list[list[int]] = []
    for match in re.finditer(r"\{([^{}]+)\},", block):
        raw_values = [item.strip() for item in match.group(1).split(',') if item.strip()]
        entries.append([parse_int_literal(value) for value in raw_values])
    return entries


def generated_strike(symbol: str, body: str, stats: dict[str, int | float]) -> dict[str, object]:
    bitmap = parse_byte_array(body, f"{symbol}Bitmap")
    page_map = parse_byte_array(body, f"{symbol}PageMap")
    if len(page_map) != 256:
        raise ValueError(f"page map for {symbol} is {len(page_map)} bytes, expected 256")

    glyph_entries = parse_record_entries(array_block(body, f"{symbol}Glyphs"))
    kern_entries = parse_record_entries(array_block(body, f"{symbol}Kerning"))
    glyph_id_entries = parse_record_entries(array_block(body, f"{symbol}GlyphIds"))

    glyphs = b"".join(
        struct.pack(RFONT4_GLYPH_FORMAT, *entry)
        for entry in glyph_entries
    )
    kern = b"".join(struct.pack(RFONT4_KERN_FORMAT, *entry) for entry in kern_entries)
    glyph_ids = b"".join(struct.pack(RFONT4_GLYPH_ID_FORMAT, *entry) for entry in glyph_id_entries)

    page_tables = bytearray()
    for index in range(int(stats["page_count"])):
        page_name = f"{symbol}Page{index:02d}"
        values = parse_uint16_array_block(array_block(body, page_name))
        if len(values) != 256:
            raise ValueError(f"page table {page_name} has {len(values)} entries, expected 256")
        page_tables.extend(struct.pack("<256H", *values))

    return {
        "glyph_count": len(glyph_entries),
        "kerning_count": len(kern_entries),
        "glyph_id_count": len(glyph_id_entries),
        "page_count": int(stats["page_count"]),
        "y_advance": int(stats["y_advance"]),
        "ascent": int(stats["ascent"]),
        "descent": int(stats["descent"]),
        "word_ink_top": int(stats["word_ink_top"]),
        "word_ink_bottom": int(stats["word_ink_bottom"]),
        "max_width": int(max((entry[3] for entry in glyph_entries), default=0)),
        "max_height": int(max((entry[4] for entry in glyph_entries), default=0)),
        "pixels_per_em": int(stats["size"]),
        "bitmap": bitmap,
        "glyphs": glyphs,
        "page_map": page_map,
        "page_tables": bytes(page_tables),
        "kerning": kern,
        "glyph_ids": glyph_ids,
    }


def pack_rfont4_family(
    display_name: str,
    locales: tuple[str, ...],
    strikes: list[dict[str, object]],
    script_mask: int,
    units_per_em: int,
    source_glyph_count: int,
    layout_tables: dict[str, bytes],
) -> bytes:
    if len(strikes) != len(SIZE_LABELS):
        raise ValueError("RFont4 requires large, medium, and small strikes")
    selected_tables = [(tag, layout_tables[tag]) for tag in FONT_LAYOUT_TAGS if layout_tables.get(tag)]
    name = display_name.encode("utf-8") + b"\0"
    locale_data = b"".join(locale.encode("ascii") + b"\0" for locale in locales)
    data = bytearray(RFONT4_HEADER_SIZE)
    name_offset = len(data)
    data.extend(name)
    locale_offset = len(data)
    data.extend(locale_data)
    strikes_offset = len(data)
    data.extend(b"\0" * (RFONT4_STRIKE_SIZE * len(strikes)))

    strike_records: list[tuple[int, ...]] = []
    for strike in strikes:
        offsets: list[int] = []
        for section in ("bitmap", "glyphs", "page_map", "page_tables", "kerning", "glyph_ids"):
            offsets.append(len(data))
            data.extend(strike[section])
        strike_records.append((
            strike["glyph_count"], strike["kerning_count"], strike["glyph_id_count"], strike["page_count"],
            strike["y_advance"], strike["ascent"], strike["descent"], strike["word_ink_top"],
            strike["word_ink_bottom"], strike["max_width"], strike["max_height"], strike["pixels_per_em"], 0,
            len(strike["bitmap"]), len(strike["page_map"]), len(strike["page_tables"]), *offsets,
        ))

    layout_tables_offset = len(data)
    data.extend(b"\0" * (RFONT4_LAYOUT_TABLE_SIZE * len(selected_tables)))
    table_records: list[tuple[int, int, int]] = []
    for tag, table in selected_tables:
        data.extend(b"\0" * (-len(data) % 4))
        table_records.append((int.from_bytes(tag.encode("ascii"), "big"), len(data), len(table)))
        data.extend(table)

    struct.pack_into(
        RFONT4_HEADER_FORMAT,
        data,
        0,
        RFONT4_MAGIC,
        RFONT4_VERSION,
        RFONT4_HEADER_SIZE,
        RFONT4_STRIKE_SIZE,
        RFONT4_GLYPH_SIZE,
        RFONT4_KERN_SIZE,
        RFONT4_GLYPH_ID_SIZE,
        RFONT4_LAYOUT_TABLE_SIZE,
        len(strikes),
        len(selected_tables),
        len(name),
        units_per_em if selected_tables else 0,
        len(locale_data),
        source_glyph_count if selected_tables else 0,
        script_mask,
        name_offset,
        locale_offset,
        strikes_offset,
        layout_tables_offset,
        len(data),
    )
    for index, record in enumerate(strike_records):
        struct.pack_into(RFONT4_STRIKE_FORMAT, data, strikes_offset + index * RFONT4_STRIKE_SIZE, *record)
    for index, record in enumerate(table_records):
        struct.pack_into(
            RFONT4_LAYOUT_TABLE_FORMAT,
            data,
            layout_tables_offset + index * RFONT4_LAYOUT_TABLE_SIZE,
            *record,
        )
    return bytes(data)


def write_rfont4(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", required=True, type=Path)
    parser.add_argument("--fallback-font", type=Path, default=None)
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--name", default=None)
    parser.add_argument("--locales", default="", help="comma-separated BCP 47 locale affinities")
    parser.add_argument("--scripts", default="", help="additional complete ISO 15924 capabilities")
    parser.add_argument("--sizes", default="large=52,medium=43,small=33")
    parser.add_argument("--map", default=DEFAULT_MAP, help="codepoint ranges, or 'auto' for every mapped glyph")
    parser.add_argument("--output-root", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--header", action="store_true", help="emit the built-in C++ fallback instead of .rfont4 files")
    parser.add_argument("--shaping", action="store_true", help="embed OpenType layout data and its glyph closure")
    parser.add_argument("--alpha-cutoff", type=int, default=DEFAULT_ALPHA_CUTOFF)
    parser.add_argument("--gamma", type=float, default=DEFAULT_GAMMA)
    parser.add_argument("--no-kerning", action="store_true")
    parser.add_argument("--no-despeckle", action="store_true")
    parser.add_argument("--despeckle-weak-threshold", type=int, default=2)
    parser.add_argument("--despeckle-strong-threshold", type=int, default=6)
    parser.add_argument("--despeckle-max-neighbors", type=int, default=2)
    args = parser.parse_args()

    codepoints = mapped_codepoints(args.font, args.map)
    named_sizes = parse_size_spec(args.sizes)
    layout_upm, source_glyph_count, layout_tables, shaping_glyph_ids = (
        font_layout(args.font, codepoints) if args.shaping and not args.header else (0, 0, {}, set())
    )
    font_display_name = args.name or args.font.stem
    locales = parse_locales(args.locales)
    declared_scripts = parse_scripts(args.scripts)
    base = c_identifier(font_display_name)
    cutoff = max(0, min(254, args.alpha_cutoff))
    gamma = max(0.01, args.gamma)
    weak = max(0, min(15, args.despeckle_weak_threshold))
    strong = max(1, min(15, args.despeckle_strong_threshold))
    max_neighbors = max(0, min(8, args.despeckle_max_neighbors))

    upm = units_per_em(args.font)
    kerning_units = {} if args.no_kerning or args.shaping else gpos_kerning_units(args.font, codepoints)

    all_stats: list[dict[str, int | float]] = []

    if args.header:
        default_header_output = Path(__file__).resolve().parent.parent / "src" / "fonts" / f"{base}.h"
        output = args.output or default_header_output
        parts: list[str] = []
        parts.append("#pragma once")
        parts.append("")
        parts.append("#include <Arduino.h>")
        parts.append('#include "fonts/AlphaFont.h"')
        parts.append("")
        parts.append("namespace ui::fonts {")
        parts.append("")
        parts.append("// Generated by fonts/convert_alpha4_font.py.")
        parts.append("// Pixel data is Alpha4 coverage, not final RGB color.")
        parts.append("// Rows are packed independently: rowStride = (width + 1) / 2 bytes.")
        parts.append("// Glyph boxes are cropped after cutoff/gamma/despeckle.")
        parts.append("// Visible spans are found directly from the packed row.")
        parts.append("// Unicode page tables provide O(1) glyph lookup by codepoint high/low byte.")
        parts.append("// Kerning pairs are stored as per-left-glyph slices.")
        if args.fallback_font is not None:
            parts.append(f"// Missing glyphs are filled from fallback font: {args.fallback_font}")
        parts.append(f"// Alpha cutoff: {cutoff}; gamma: {gamma:g}; despeckle: {not args.no_despeckle}")
        parts.append(f"// Codepoint map: {args.map}")
        parts.append("")

        font_symbols: list[str] = []
        for _, size in named_sizes:
            symbol = f"{base}_{size}"
            body, stats = generate_font(
                args.font,
                args.fallback_font,
                size,
                symbol,
                codepoints,
                shaping_glyph_ids,
                kerning_units,
                upm,
                cutoff,
                gamma,
                not args.no_despeckle,
                weak,
                strong,
                max_neighbors,
            )
            parts.append(body)
            font_symbols.append(symbol)
            all_stats.append(stats)

        parts.append(f"inline constexpr const AlphaFont* {base}_Sizes[] = {{")
        for symbol in font_symbols:
            parts.append(f"    &{symbol},")
        parts.append("};")
        parts.append("")
        parts.append("} // namespace ui::fonts")
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text("\n".join(parts), encoding="utf-8")
        print(f"wrote {output}")
    else:
        output_root = args.output or args.output_root
        family_dir = output_root / font_display_name
        generated_strikes: list[dict[str, object]] = []
        for label, size in named_sizes:
            symbol = f"{base}_{size}"
            body, stats = generate_font(
                args.font,
                args.fallback_font,
                size,
                symbol,
                codepoints,
                shaping_glyph_ids,
                kerning_units,
                upm,
                cutoff,
                gamma,
                not args.no_despeckle,
                weak,
                strong,
                max_neighbors,
            )
            generated_strikes.append(generated_strike(symbol, body, stats))
            all_stats.append(stats)
        family_script_mask = capability_mask(
            [int(stats["script_mask"]) for stats in all_stats], declared_scripts
        )
        target = family_dir / "font.rfont4"
        write_rfont4(
            target,
            pack_rfont4_family(
                font_display_name,
                locales,
                generated_strikes,
                family_script_mask,
                layout_upm,
                source_glyph_count,
                layout_tables,
            ),
        )
        print(f"wrote {target}")
        index_path = output_root / "index.json"
        catalog: list[dict[str, object]] = []
        if index_path.exists():
            try:
                catalog = json.loads(index_path.read_text(encoding="utf-8"))
            except json.JSONDecodeError:
                catalog = []
        item = {
            "id": normalize_catalog_id(font_display_name),
            "name": font_display_name,
            "locales": list(locales),
            "scriptMask": family_script_mask,
            "file": str(target.relative_to(output_root)).replace('\\', '/'),
            "shaping": bool(layout_tables),
        }
        catalog = [entry for entry in catalog if entry.get("id") != item["id"]]
        catalog.append(item)
        catalog.sort(key=lambda entry: str(entry.get("name", "")).lower())
        index_path.write_text(json.dumps(catalog, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"wrote {index_path}")

    for stats in all_stats:
        ratio4 = (stats["bitmap_bytes"] / stats["raw_alpha4_bytes"]) if stats["raw_alpha4_bytes"] else 0
        ratio8 = (stats["bitmap_bytes"] / stats["raw_alpha8_bytes"]) if stats["raw_alpha8_bytes"] else 0
        print(
            "size={size} glyphs={glyphs} missing={missing} fallback={fallback_glyphs} bitmap={bitmap_bytes} "
            "pages={page_count} shapingGlyphs={shaping_glyphs} "
            "rawAlpha4={raw_alpha4_bytes} rawAlpha8={raw_alpha8_bytes} "
            "ratio4={ratio4:.2f} ratio8={ratio8:.2f} yAdvance={y_advance} ascent={ascent} "
            "descent={descent} wordInk={word_ink_top}..{word_ink_bottom} kerningPairs={kerning_pairs} "
            "scripts=0x{script_mask:08X}".format(
                **stats, ratio4=ratio4, ratio8=ratio8
            )
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
