"""Minimal BDF to U8g2 encoder for generated locale UI fonts."""

from __future__ import annotations

from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True, slots=True)
class Glyph:
	codepoint: int
	advance: int
	width: int
	x: int
	y: int
	bitmap: tuple[int, ...]

	@property
	def height(self) -> int:
		return len(self.bitmap)


@dataclass(frozen=True, slots=True)
class Box:
	width: int
	height: int
	x: int
	y: int


class Bits:
	def __init__(self, prefix: bytes = b"") -> None:
		self.data = bytearray(prefix)
		self.position = len(prefix) * 8

	def add(self, width: int, value: int) -> None:
		if width <= 0 or not 0 <= value < 1 << width:
			raise ValueError(f"{value} does not fit in {width} bits")
		for index in range(width):
			byte = self.position // 8
			if byte == len(self.data):
				self.data.append(0)
			self.data[byte] |= ((value >> index) & 1) << (self.position & 7)
			self.position += 1

	def finish(self) -> bytes:
		return bytes(self.data)


def _bdf_lines(path: Path) -> Iterator[str]:
	with path.open(encoding="ascii") as file:
		for line in file:
			yield line.rstrip("\r\n")


def _parse_bdf(path: Path, wanted: set[int]) -> dict[int, Glyph]:
	glyphs: dict[int, Glyph] = {}
	lines = iter(_bdf_lines(path))
	for line in lines:
		if not line.startswith("STARTCHAR"):
			continue
		codepoint = -1
		advance = 0
		box: Box | None = None
		bitmap: tuple[int, ...] | None = None
		for line in lines:
			if line.startswith("ENCODING "):
				codepoint = int(line.split()[1])
			elif line.startswith("DWIDTH "):
				advance = int(line.split()[1])
			elif line.startswith("BBX "):
				width, height, x, y = map(int, line.split()[1:])
				box = Box(width, height, x, y)
			elif line == "BITMAP":
				if box is None:
					raise ValueError(f"{path} has BITMAP before BBX")
				rows: list[int] = []
				for _ in range(box.height):
					hex_row = next(lines)
					if codepoint in wanted:
						rows.append(int(hex_row, 16) >> (len(hex_row) * 4 - box.width))
				if codepoint in wanted:
					bitmap = tuple(rows)
			elif line == "ENDCHAR":
				break
		if codepoint not in wanted:
			continue
		if box is None or bitmap is None:
			raise ValueError(f"{path} glyph U+{codepoint:04X} is incomplete")
		if codepoint in glyphs:
			raise ValueError(f"{path} contains duplicate U+{codepoint:04X}")
		glyphs[codepoint] = _trim(Glyph(codepoint, advance, box.width, box.x, box.y, bitmap))
	return glyphs


def _parse_outline(path: Path, wanted: set[int], pixel_size: int) -> dict[int, Glyph]:
	try:
		import freetype
	except ImportError as exc:
		raise RuntimeError("Outline UI generation requires freetype-py") from exc

	face = freetype.Face(str(path))
	face.set_pixel_sizes(0, pixel_size)
	flags = freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO | freetype.FT_LOAD_MONOCHROME
	glyphs: dict[int, Glyph] = {}
	for codepoint in wanted:
		if face.get_char_index(codepoint) == 0:
			continue
		face.load_char(codepoint, flags)
		bitmap = face.glyph.bitmap
		pitch = abs(bitmap.pitch)
		data = bytes(bitmap.buffer)
		rows = []
		for y in range(bitmap.rows):
			row = y if bitmap.pitch >= 0 else bitmap.rows - 1 - y
			packed = int.from_bytes(data[row * pitch : (row + 1) * pitch], "big")
			rows.append(packed >> (pitch * 8 - bitmap.width))
		glyphs[codepoint] = _trim(
			Glyph(
				codepoint,
				(face.glyph.advance.x + 32) // 64,
				bitmap.width,
				face.glyph.bitmap_left,
				face.glyph.bitmap_top - bitmap.rows,
				tuple(rows),
			)
		)
	return glyphs


def _trim(glyph: Glyph) -> Glyph:
	mask = 0
	for row in glyph.bitmap:
		mask |= row
	if not mask:
		return Glyph(glyph.codepoint, glyph.advance, 0, glyph.x, glyph.y + glyph.height, ())
	top = next(index for index, row in enumerate(glyph.bitmap) if row)
	bottom = len(glyph.bitmap) - 1 - next(index for index, row in enumerate(reversed(glyph.bitmap)) if row)
	left = glyph.width - mask.bit_length()
	right = (mask & -mask).bit_length() - 1
	width = glyph.width - left - right
	return Glyph(
		glyph.codepoint,
		glyph.advance,
		width,
		glyph.x + left,
		glyph.y + glyph.height - 1 - bottom,
		tuple((row >> right) & ((1 << width) - 1) for row in glyph.bitmap[top : bottom + 1]),
	)


def _max_box(glyphs: list[Glyph]) -> Box:
	left = min(glyph.x for glyph in glyphs)
	right = max(glyph.x + glyph.width for glyph in glyphs)
	bottom = min(glyph.y for glyph in glyphs)
	top = max(glyph.y + glyph.height for glyph in glyphs)
	return Box(right - left, top - bottom, left, bottom)


def _local_box(glyph: Glyph, maximum: Box) -> tuple[Box, int]:
	width = glyph.width - glyph.x if glyph.x < 0 else glyph.width + glyph.x
	return Box(max(width, glyph.advance), maximum.height, 0, maximum.y), min(glyph.x, 0)


def _unsigned_bits(value: int) -> int:
	return value.bit_length()


def _signed_bits(value: int) -> int:
	return ((-value - 1).bit_length() if value < 0 else value.bit_length()) + 1


def _fields(glyphs: list[Glyph], maximum: Box) -> tuple[int, int, int, int, int]:
	boxes = [_local_box(glyph, maximum)[0] for glyph in glyphs]
	return (
		max(_unsigned_bits(box.width) for box in boxes),
		max(_unsigned_bits(box.height) for box in boxes),
		max(_signed_bits(box.x) for box in boxes),
		max(_signed_bits(box.y) for box in boxes),
		max(_signed_bits(box.width) for box in boxes),
	)


def _pixel(glyph: Glyph, x: int, y: int, shift: int) -> bool:
	x += shift
	if not glyph.x <= x < glyph.x + glyph.width or not glyph.y <= y < glyph.y + glyph.height:
		return False
	row = glyph.bitmap[glyph.height - 1 - (y - glyph.y)]
	return bool(row & (1 << (glyph.width - 1 - (x - glyph.x))))


def _runs(glyph: Glyph, box: Box, shift: int) -> list[tuple[int, int]]:
	values = [
		_pixel(glyph, x, y, shift)
		for y in range(box.y + box.height - 1, box.y - 1, -1)
		for x in range(box.x, box.x + box.width)
	]
	runs: list[int] = []
	wanted = False
	count = 0
	for value in values:
		if value == wanted:
			count += 1
		else:
			runs.append(count)
			wanted = value
			count = 1
	runs.append(count)
	if len(runs) & 1:
		runs.append(0)
	return list(zip(runs[::2], runs[1::2]))


def _add_rle(bits: Bits, pairs: list[tuple[int, int]], zero_bits: int, one_bits: int) -> None:
	last: tuple[int, int] | None = None
	zero_limit = (1 << zero_bits) - 1
	one_limit = (1 << one_bits) - 1
	for zeros, ones in pairs:
		while zeros > zero_limit:
			last = _add_pair(bits, last, (zero_limit, 0), zero_bits, one_bits)
			zeros -= zero_limit
		while ones > one_limit:
			last = _add_pair(bits, last, (zeros, one_limit), zero_bits, one_bits)
			zeros = 0
			ones -= one_limit
		if zeros or ones:
			last = _add_pair(bits, last, (zeros, ones), zero_bits, one_bits)
	bits.add(1, 0)


def _add_pair(
	bits: Bits,
	last: tuple[int, int] | None,
	pair: tuple[int, int],
	zero_bits: int,
	one_bits: int,
) -> tuple[int, int]:
	if pair == last:
		bits.add(1, 1)
	else:
		if last is not None:
			bits.add(1, 0)
		bits.add(zero_bits, pair[0])
		bits.add(one_bits, pair[1])
	return pair


def _record(
	glyph: Glyph,
	maximum: Box,
	field_bits: tuple[int, int, int, int, int],
	runs: list[tuple[int, int]],
	zero_bits: int,
	one_bits: int,
) -> bytes:
	box = _local_box(glyph, maximum)[0]
	prefix = glyph.codepoint.to_bytes(1 if glyph.codepoint <= 0xFF else 2, "big") + b"\0"
	bits = Bits(prefix)
	width_bits, height_bits, x_bits, y_bits, advance_bits = field_bits
	bits.add(width_bits, box.width)
	bits.add(height_bits, box.height)
	bits.add(x_bits, box.x + (1 << (x_bits - 1)))
	bits.add(y_bits, box.y + (1 << (y_bits - 1)))
	bits.add(advance_bits, box.width + (1 << (advance_bits - 1)))
	_add_rle(bits, runs, zero_bits, one_bits)
	record = bytearray(bits.finish())
	if len(record) >= 0xFF:
		raise ValueError(f"U+{glyph.codepoint:04X} is too large for U8g2")
	record[len(prefix) - 1] = len(record)
	return bytes(record)


def _metric(glyphs: dict[int, Glyph], codepoint: int, *, ascent: bool) -> int:
	glyph = glyphs.get(codepoint)
	if glyph is None:
		return 0
	return glyph.y + glyph.height if ascent else glyph.y


def _encode(path: Path, all_glyphs: dict[int, Glyph], codepoints: set[int]) -> bytes:
	if not codepoints:
		raise ValueError("UI font needs at least one codepoint")
	if max(codepoints) > 0xFFFF:
		raise ValueError("U8g2 UI fonts only support BMP codepoints")
	missing = codepoints - all_glyphs.keys()
	if missing:
		formatted = ", ".join(f"U+{codepoint:04X}" for codepoint in sorted(missing)[:8])
		raise ValueError(f"{path} is missing {formatted}")
	glyphs = [all_glyphs[codepoint] for codepoint in sorted(codepoints)]
	maximum = _max_box(glyphs)
	field_bits = _fields(glyphs, maximum)
	runs = {}
	for glyph in glyphs:
		box, shift = _local_box(glyph, maximum)
		runs[glyph.codepoint] = _runs(glyph, box, shift)

	best: tuple[int, int] | None = None
	best_size: int | None = None
	for zero_bits in range(2, 9):
		for one_bits in range(2, 7):
			size = sum(
				len(_record(glyph, maximum, field_bits, runs[glyph.codepoint], zero_bits, one_bits))
				for glyph in glyphs
			)
			if best_size is None or size < best_size:
				best = zero_bits, one_bits
				best_size = size
	assert best is not None
	zero_bits, one_bits = best
	records = {
		glyph.codepoint: _record(
			glyph, maximum, field_bits, runs[glyph.codepoint], zero_bits, one_bits
		)
		for glyph in glyphs
	}

	cap_ascent = _metric(all_glyphs, ord("A"), ascent=True) or _metric(
		all_glyphs, ord("1"), ascent=True
	)
	descent = _metric(all_glyphs, ord("g"), ascent=False)
	paren = all_glyphs.get(ord("("))
	paren_ascent = paren.y + paren.height if paren else cap_ascent
	paren_descent = paren.y if paren else descent
	header = bytearray(
		[
			len(glyphs) & 0xFF,
			1,
			zero_bits,
			one_bits,
			*field_bits,
			maximum.width & 0xFF,
			maximum.height & 0xFF,
			maximum.x & 0xFF,
			maximum.y & 0xFF,
			cap_ascent & 0xFF,
			descent & 0xFF,
			paren_ascent & 0xFF,
			paren_descent & 0xFF,
			0,
			0,
			0,
			0,
			0,
			0,
		]
	)
	for codepoint in sorted(codepoints & set(range(0x100))):
		header.extend(records[codepoint])
	header.extend(b"\0\0")
	unicode_start = len(header) - 23
	unicode_records = [records[codepoint] for codepoint in sorted(codepoints) if codepoint >= 0x100]
	lookup_count = len(unicode_records) // 101
	lookup_start = len(header)
	header.extend(b"\0\0\0\0" * max(lookup_count - 1, 0))
	header.extend(b"\0\4\xff\xff")
	last_delta = len(header) - lookup_start
	last_target = len(header)
	lookup_index = 0
	for index, record in enumerate(unicode_records, 1):
		header.extend(record)
		if index % 101 == 0 and lookup_index < lookup_count:
			entry = lookup_start + lookup_index * 4
			header[entry : entry + 2] = last_delta.to_bytes(2, "big")
			header[entry + 2] |= record[0]
			header[entry + 3] |= record[1]
			lookup_index += 1
			last_delta = len(header) - last_target
			last_target = len(header)
	if lookup_index < lookup_count:
		entry = lookup_start + lookup_index * 4
		header[entry : entry + 4] = last_delta.to_bytes(2, "big") + b"\xff\xff"
	header.extend(b"\0\0")

	position = 23
	while header[position + 1]:
		codepoint = header[position]
		if codepoint == ord("A"):
			header[17:19] = (position - 23).to_bytes(2, "big")
		elif codepoint == ord("a"):
			header[19:21] = (position - 23).to_bytes(2, "big")
		position += header[position + 1]
	header[21:23] = unicode_start.to_bytes(2, "big")
	return bytes(header)


def encode_bdf(path: Path, codepoints: set[int]) -> bytes:
	"""Encode exactly ``codepoints`` from a BDF as a U8g2 height-mode font."""
	wanted = codepoints | {ord("A"), ord("1"), ord("g"), ord("(")}
	return _encode(path, _parse_bdf(path, wanted), codepoints)


def encode_outline(path: Path, codepoints: set[int], pixel_size: int) -> bytes:
	"""Rasterize and encode exactly ``codepoints`` from a pixel outline font."""
	if pixel_size <= 0:
		raise ValueError("Outline UI font pixel size must be positive")
	wanted = codepoints | {ord("A"), ord("1"), ord("g"), ord("(")}
	return _encode(path, _parse_outline(path, wanted, pixel_size), codepoints)
