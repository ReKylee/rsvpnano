#!/usr/bin/env python3
"""Convert TrueType/OpenType fonts to sparse Alpha4 Arduino_GFX bitfonts.

This generator intentionally moves font-shape work out of the firmware runtime:

* FreeType rasterization, cutoff/gamma, and despeckling are done offline.
* Glyph boxes are cropped from the final Alpha4 mask, not raw coverage.
* Rows are packed independently as Alpha4, two pixels per byte.
* Non-transparent row spans are generated so runtime doesn't scan glyph rows.
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
from dataclasses import dataclass
from pathlib import Path

import freetype

try:
    from fontTools.ttLib import TTFont
except Exception:  # pragma: no cover
    TTFont = None  # type: ignore[assignment]

DEFAULT_MAP = "32-126,160-255,256-383,1024-1279,8208-8230,8240,8249,8250,8364,8470"
DEFAULT_ALPHA_CUTOFF = 32
DEFAULT_GAMMA = 1.15
MISSING_GLYPH_INDEX = 0xFFFF
MISSING_PAGE_INDEX = 0xFF


@dataclass(frozen=True)
class CodepointRange:
    start: int
    end: int


@dataclass(frozen=True)
class GlyphRecord:
    codepoint: int
    bitmap_offset: int
    row_offset: int
    kern_offset: int
    width: int
    height: int
    row_stride: int
    x_advance: int
    x_offset: int
    y_offset: int
    kern_count: int


@dataclass(frozen=True)
class RowRecord:
    span_offset: int
    span_count: int


@dataclass(frozen=True)
class SpanRecord:
    x: int
    width: int


@dataclass(frozen=True)
class RenderedGlyph:
    codepoint: int
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
            ranges.append(CodepointRange(max(0, start), min(0xFFFF, end)))
        else:
            value = int(token, 0)
            if 0 <= value <= 0xFFFF:
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


def codepoints_from_ranges(ranges: list[CodepointRange]) -> list[int]:
    return [cp for item in ranges for cp in range(item.start, item.end + 1)]


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


def spans_for_row(row: list[int]) -> list[SpanRecord]:
    spans: list[SpanRecord] = []
    x = 0
    width = len(row)
    while x < width:
        while x < width and row[x] == 0:
            x += 1
        if x >= width:
            break
        start = x
        while x < width and row[x] != 0:
            x += 1
        spans.append(SpanRecord(start, x - start))
    return spans


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
    if face.get_char_index(codepoint) == 0 and codepoint not in (ord(' '), ord('?')):
        return None

    try:
        face.load_char(chr(codepoint), freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_NORMAL)
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
        return RenderedGlyph(codepoint, [], 0, 0, 0, x_advance, 0, 0)

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
        return RenderedGlyph(codepoint, [], 0, 0, 0, x_advance, 0, 0)

    height = min(255, len(cropped))
    width = min(255, len(cropped[0]))
    cropped = [row[:width] for row in cropped[:height]]

    return RenderedGlyph(
        codepoint=codepoint,
        rows=cropped,
        width=width,
        height=height,
        row_stride=row_stride_bytes(width),
        x_advance=x_advance,
        x_offset=max(-128, min(127, x_offset + min_x)),
        y_offset=max(-128, min(127, y_offset + min_y)),
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
    for high in sorted({cp >> 8 for cp in glyph_index_by_codepoint}):
        table = [MISSING_GLYPH_INDEX] * 256
        for codepoint, glyph_index in glyph_index_by_codepoint.items():
            if (codepoint >> 8) == high:
                table[codepoint & 0xFF] = glyph_index
        pages_by_high[high] = table

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
    rows: list[RowRecord] = []
    spans: list[SpanRecord] = []
    records: list[GlyphRecord] = []
    glyph_index_by_codepoint: dict[int, int] = {}
    rendered_by_codepoint: dict[int, RenderedGlyph] = {}
    missing = 0
    fallback_glyphs = 0

    for codepoint in codepoints:
        rendered = render_glyph(face, codepoint, cutoff, gamma, enable_despeckle, weak_threshold, strong_threshold, max_neighbors)
        if rendered is None and fallback_face is not None:
            rendered = render_glyph(fallback_face, codepoint, cutoff, gamma, enable_despeckle, weak_threshold, strong_threshold, max_neighbors)
            if rendered is not None:
                fallback_glyphs += 1
        if rendered is None:
            missing += 1
            continue
        rendered_by_codepoint[codepoint] = rendered
        glyph_index_by_codepoint[codepoint] = len(records)
        records.append(GlyphRecord(
            codepoint=codepoint,
            bitmap_offset=0,
            row_offset=0,
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
    for record in records:
        rendered = rendered_by_codepoint[record.codepoint]
        bitmap_offset = len(bitmap)
        row_offset = len(rows)

        if rendered.width > 0 and rendered.height > 0:
            bitmap.extend(pack_alpha4_rows(rendered.rows, rendered.width, rendered.height))
            for row in rendered.rows:
                row_spans = spans_for_row(row)
                rows.append(RowRecord(len(spans), len(row_spans)))
                spans.extend(row_spans)

        unique_pairs = sorted(set(pairs_by_left.get(record.codepoint, [])), key=lambda item: item[0])
        kern_offset = len(kern_pairs_flat)
        kern_pairs_flat.extend(unique_pairs)

        final_records.append(GlyphRecord(
            codepoint=record.codepoint,
            bitmap_offset=bitmap_offset,
            row_offset=row_offset,
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
    page_map, pages = build_page_tables(glyph_index_by_codepoint)

    ascent = max(0, min(255, int(math.ceil(face.size.ascender / 64.0))))
    descent = max(0, min(255, int(math.ceil(abs(face.size.descender / 64.0)))))
    y_advance = max(1, min(255, int(math.ceil(face.size.height / 64.0))))
    word_ink_top, word_ink_bottom = word_ink_metrics(records)

    bitmap_name = f"{symbol}Bitmap"
    glyph_name = f"{symbol}Glyphs"
    row_name = f"{symbol}Rows"
    span_name = f"{symbol}Spans"
    page_map_name = f"{symbol}PageMap"
    pages_name = f"{symbol}Pages"
    kern_name = f"{symbol}Kerning"

    lines: list[str] = []
    lines.append(f"constexpr uint8_t {symbol}MaxGlyphWidth = {max((rec.width for rec in records), default=0)};")
    lines.append(f"constexpr uint8_t {symbol}MaxGlyphHeight = {max((rec.height for rec in records), default=0)};")
    lines.append(f"constexpr uint8_t {symbol}MaxRowSpanCount = {max((row.span_count for row in rows), default=0)};")
    lines.append("")

    lines.append(f"inline constexpr uint8_t {bitmap_name}[] PROGMEM = {{")
    if bitmap:
        lines.append(emit_bytes(bytes(bitmap)))
    lines.append("};")
    lines.append("")

    lines.append(f"inline constexpr AlphaRow {row_name}[] PROGMEM = {{")
    for row in rows:
        lines.append(f"    {{{row.span_offset}UL, {row.span_count}}},")
    lines.append("};")
    lines.append("")

    lines.append(f"inline constexpr AlphaSpan {span_name}[] PROGMEM = {{")
    for span in spans:
        lines.append(f"    {{{span.x}, {span.width}}},")
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
            f"    {{0x{rec.codepoint:04X}, {rec.bitmap_offset}UL, {rec.row_offset}UL, {rec.kern_offset}, "
            f"{rec.width}, {rec.height}, {rec.row_stride}, {rec.x_advance}, "
            f"{rec.x_offset}, {rec.y_offset}, {rec.kern_count}}},"
        )
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
    lines.append(f"    {row_name},")
    lines.append(f"    {span_name},")
    lines.append(f"    {page_map_name},")
    lines.append(f"    {pages_name},")
    lines.append(f"    {len(pages)},")
    lines.append(f"    {kern_name},")
    lines.append(f"    {len(kern_pairs_flat)},")
    lines.append(f"    {word_ink_top},")
    lines.append(f"    {word_ink_bottom},")
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
        "row_count": len(rows),
        "span_count": len(spans),
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
    }
    return "\n".join(lines), stats


SIZE_LABELS = ("large", "medium", "small")
RFONT4_MAGIC = 0x34544652
RFONT4_VERSION = 1
RFONT4_HEADER_FORMAT = "<I" + "H" * 11 + "IIIHBBBbbBBBB" + "I" * 10
RFONT4_GLYPH_FORMAT = "<HIIHBBBBbbB"
RFONT4_ROW_FORMAT = "<IB"
RFONT4_SPAN_FORMAT = "<BB"
RFONT4_KERN_FORMAT = "<Hb"
RFONT4_HEADER_SIZE = struct.calcsize(RFONT4_HEADER_FORMAT)
RFONT4_GLYPH_SIZE = struct.calcsize(RFONT4_GLYPH_FORMAT)
RFONT4_ROW_SIZE = struct.calcsize(RFONT4_ROW_FORMAT)
RFONT4_SPAN_SIZE = struct.calcsize(RFONT4_SPAN_FORMAT)
RFONT4_KERN_SIZE = struct.calcsize(RFONT4_KERN_FORMAT)


def normalize_catalog_id(name: str) -> str:
    ident = re.sub(r"[^0-9A-Za-z]+", "-", name).strip("-").lower()
    return ident or "font"


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


def pack_generated_rfont4(symbol: str, display_name: str, body: str, stats: dict[str, int | float]) -> bytes:
    bitmap = parse_byte_array(body, f"{symbol}Bitmap")
    page_map = parse_byte_array(body, f"{symbol}PageMap")
    if len(page_map) != 256:
        raise ValueError(f"page map for {symbol} is {len(page_map)} bytes, expected 256")

    glyph_entries = parse_record_entries(array_block(body, f"{symbol}Glyphs"))
    row_entries = parse_record_entries(array_block(body, f"{symbol}Rows"))
    span_entries = parse_record_entries(array_block(body, f"{symbol}Spans"))
    kern_entries = parse_record_entries(array_block(body, f"{symbol}Kerning"))

    glyphs = b"".join(struct.pack(RFONT4_GLYPH_FORMAT, *entry) for entry in glyph_entries)
    rows = b"".join(struct.pack(RFONT4_ROW_FORMAT, *entry) for entry in row_entries)
    spans = b"".join(struct.pack(RFONT4_SPAN_FORMAT, *entry) for entry in span_entries)
    kern = b"".join(struct.pack(RFONT4_KERN_FORMAT, *entry) for entry in kern_entries)

    page_table_chunks: list[bytes] = []
    page_count = 0
    for index in range(256):
        page_name = f"{symbol}Page{index:02d}"
        try:
            block = array_block(body, page_name)
        except ValueError:
            break
        values = parse_uint16_array_block(block)
        if len(values) != 256:
            raise ValueError(f"page table {page_name} has {len(values)} entries, expected 256")
        page_table_chunks.append(struct.pack("<" + "H" * len(values), *values))
        page_count += 1
    page_tables = b"".join(page_table_chunks)

    name_bytes = display_name.encode('utf-8') + b"\0"
    offset = RFONT4_HEADER_SIZE
    name_offset = offset; offset += len(name_bytes)
    bitmap_offset = offset; offset += len(bitmap)
    glyphs_offset = offset; offset += len(glyphs)
    rows_offset = offset; offset += len(rows)
    spans_offset = offset; offset += len(spans)
    page_map_offset = offset; offset += len(page_map)
    page_tables_offset = offset; offset += len(page_tables)
    kerning_offset = offset; offset += len(kern)
    total_size = offset

    header = struct.pack(
        RFONT4_HEADER_FORMAT,
        RFONT4_MAGIC,
        RFONT4_VERSION,
        RFONT4_HEADER_SIZE,
        RFONT4_GLYPH_SIZE,
        RFONT4_ROW_SIZE,
        RFONT4_SPAN_SIZE,
        RFONT4_KERN_SIZE,
        len(glyph_entries),
        len(row_entries),
        len(span_entries),
        page_count,
        len(kern_entries),
        len(bitmap),
        len(page_map),
        len(page_tables),
        len(name_bytes),
        int(stats["y_advance"]),
        int(stats["ascent"]),
        int(stats["descent"]),
        int(stats["word_ink_top"]),
        int(stats["word_ink_bottom"]),
        int(max((entry[4] for entry in glyph_entries), default=0)),
        int(max((entry[5] for entry in glyph_entries), default=0)),
        int(max((entry[1] for entry in row_entries), default=0)),
        0,
        name_offset,
        bitmap_offset,
        glyphs_offset,
        rows_offset,
        spans_offset,
        page_map_offset,
        page_tables_offset,
        kerning_offset,
        total_size,
        0,
    )
    assert len(header) == RFONT4_HEADER_SIZE
    return header + name_bytes + bitmap + glyphs + rows + spans + page_map + page_tables + kern


def write_rfont4(path: Path, symbol: str, display_name: str, body: str, stats: dict[str, int | float]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(pack_generated_rfont4(symbol, display_name, body, stats))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", required=True, type=Path)
    parser.add_argument("--fallback-font", type=Path, default=None)
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--name", default=None)
    parser.add_argument("--sizes", default="large=52,medium=43,small=33")
    parser.add_argument("--map", default=DEFAULT_MAP)
    parser.add_argument("--output-root", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--header", action="store_true", help="emit the legacy C++ header fallback instead of .rfont4 files")
    parser.add_argument("--alpha-cutoff", type=int, default=DEFAULT_ALPHA_CUTOFF)
    parser.add_argument("--gamma", type=float, default=DEFAULT_GAMMA)
    parser.add_argument("--no-kerning", action="store_true")
    parser.add_argument("--no-despeckle", action="store_true")
    parser.add_argument("--despeckle-weak-threshold", type=int, default=2)
    parser.add_argument("--despeckle-strong-threshold", type=int, default=6)
    parser.add_argument("--despeckle-max-neighbors", type=int, default=2)
    args = parser.parse_args()

    ranges = parse_codepoint_ranges(args.map)
    codepoints = codepoints_from_ranges(ranges)
    named_sizes = parse_size_spec(args.sizes)
    font_display_name = args.name or args.font.stem
    base = c_identifier(font_display_name)
    cutoff = max(0, min(254, args.alpha_cutoff))
    gamma = max(0.01, args.gamma)
    weak = max(0, min(15, args.despeckle_weak_threshold))
    strong = max(1, min(15, args.despeckle_strong_threshold))
    max_neighbors = max(0, min(8, args.despeckle_max_neighbors))

    upm = units_per_em(args.font)
    kerning_units = {} if args.no_kerning else gpos_kerning_units(args.font, codepoints)

    all_stats: list[dict[str, int | float]] = []

    if args.header:
        default_header_output = Path(__file__).resolve().parent.parent / "src" / "fonts" / f"{base}.h"
        output = args.output or default_header_output
        parts: list[str] = []
        parts.append("#pragma once")
        parts.append("")
        parts.append("#include <Arduino.h>")
        parts.append('#include "display/AlphaFont.h"')
        parts.append("")
        parts.append("// Generated by fonts/convert_alpha4_font.py.")
        parts.append("// Pixel data is Alpha4 coverage, not final RGB color.")
        parts.append("// Rows are packed independently: rowStride = (width + 1) / 2 bytes.")
        parts.append("// Glyph boxes are cropped after cutoff/gamma/despeckle.")
        parts.append("// Row-span metadata avoids runtime glyph-row scanning.")
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
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text("\n".join(parts), encoding="utf-8")
        print(f"wrote {output}")
    else:
        output_root = args.output or args.output_root
        family_dir = output_root / font_display_name
        catalog_files: dict[str, str] = {}
        for label, size in named_sizes:
            symbol = f"{base}_{size}"
            body, stats = generate_font(
                args.font,
                args.fallback_font,
                size,
                symbol,
                codepoints,
                kerning_units,
                upm,
                cutoff,
                gamma,
                not args.no_despeckle,
                weak,
                strong,
                max_neighbors,
            )
            target = family_dir / f"{label}.rfont4"
            write_rfont4(target, symbol, f"{font_display_name} {label}", body, stats)
            catalog_files[label] = str(target.relative_to(output_root)).replace('\\', '/')
            all_stats.append(stats)
            print(f"wrote {target}")
        index_path = output_root / "index.json"
        catalog: list[dict[str, object]] = []
        if index_path.exists():
            try:
                catalog = json.loads(index_path.read_text(encoding="utf-8"))
            except json.JSONDecodeError:
                catalog = []
        item = {"id": normalize_catalog_id(font_display_name), "name": font_display_name, "files": catalog_files}
        catalog = [entry for entry in catalog if entry.get("id") != item["id"]]
        catalog.append(item)
        catalog.sort(key=lambda entry: str(entry.get("name", "")).lower())
        index_path.write_text(json.dumps(catalog, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"wrote {index_path}")

    for stats in all_stats:
        ratio4 = (stats["bitmap_bytes"] / stats["raw_alpha4_bytes"]) if stats["raw_alpha4_bytes"] else 0
        ratio8 = (stats["bitmap_bytes"] / stats["raw_alpha8_bytes"]) if stats["raw_alpha8_bytes"] else 0
        print(
            "size={size} glyphs={glyphs} missing={missing} fallback={fallback_glyphs} bitmap={bitmap_bytes} rows={row_count} "
            "spans={span_count} pages={page_count} rawAlpha4={raw_alpha4_bytes} rawAlpha8={raw_alpha8_bytes} "
            "ratio4={ratio4:.2f} ratio8={ratio8:.2f} yAdvance={y_advance} ascent={ascent} "
            "descent={descent} wordInk={word_ink_top}..{word_ink_bottom} kerningPairs={kerning_pairs}".format(
                **stats, ratio4=ratio4, ratio8=ratio8
            )
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
