#!/usr/bin/env bash
set -euo pipefail

# Convert Noto Serif Regular to a compact U8g2 C header for RSVP Nano.
#
# Dependencies:
#   - otf2bdf, for example from the otf2bdf package
#   - bdfconv, from the upstream U8g2 tools/font/bdfconv build
#
# Usage:
#   tools/convert_noto_serif_to_u8g2.sh /path/to/NotoSerif-Regular.ttf
#
# Optional environment overrides:
#   SIZE=18
#   OUT=src/fonts/NotoSerifU8g2.h
#   SYMBOL=u8g2_font_rsvpnano_noto_serif_18_tf
#   BDFCONV=/path/to/bdfconv
#   OTF2BDF=/path/to/otf2bdf

FONT_PATH=${1:-${NOTO_SERIF_TTF:-}}
SIZE=${SIZE:-18}
OUT=${OUT:-src/fonts/NotoSerifU8g2.h}
SYMBOL=${SYMBOL:-u8g2_font_rsvpnano_noto_serif_18_tf}
BDFCONV=${BDFCONV:-bdfconv}
OTF2BDF=${OTF2BDF:-otf2bdf}

if [[ -z "${FONT_PATH}" ]]; then
  echo "error: pass /path/to/NotoSerif-Regular.ttf or set NOTO_SERIF_TTF" >&2
  exit 2
fi

if [[ ! -f "${FONT_PATH}" ]]; then
  echo "error: font file not found: ${FONT_PATH}" >&2
  exit 2
fi

command -v "${OTF2BDF}" >/dev/null || {
  echo "error: otf2bdf not found. Install it or set OTF2BDF=/path/to/otf2bdf" >&2
  exit 2
}

command -v "${BDFCONV}" >/dev/null || {
  echo "error: bdfconv not found. Build U8g2 tools/font/bdfconv or set BDFCONV=/path/to/bdfconv" >&2
  exit 2
}

mkdir -p "$(dirname "${OUT}")" .tmp/fonts
BDF=.tmp/fonts/NotoSerif-${SIZE}.bdf
TMP_OUT=.tmp/fonts/NotoSerifU8g2.generated.h

# Keep coverage compact for firmware flash while covering normal English books.
# 32-126: printable Basic Latin
# 8211/8212: en/em dash
# 8216/8217/8220/8221: curly quotes
# 8226: bullet
# 8230: ellipsis
MAP='32-126,8211,8212,8216,8217,8220,8221,8226,8230'

"${OTF2BDF}" -p "${SIZE}" -r 72 -o "${BDF}" "${FONT_PATH}"
"${BDFCONV}" -f 1 -m "${MAP}" -n "${SYMBOL}" "${BDF}" -o "${TMP_OUT}"

{
  echo '#pragma once'
  echo
  echo '#include <stdint.h>'
  echo
  echo '// Generated from Noto Serif Regular for RSVP Nano.'
  echo '// Coverage: U+0020..U+007E plus dashes, curly quotes, bullet, and ellipsis.'
  echo '// Noto Serif is licensed under the SIL Open Font License 1.1.'
  echo
  sed '/^#include/d' "${TMP_OUT}"
} > "${OUT}"

echo "wrote ${OUT}"
