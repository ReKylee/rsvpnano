#!/usr/bin/env bash
set -euo pipefail

# Generic TrueType/OpenType to U8g2 C header converter for RSVP Nano.
#
# Dependencies:
#   - otf2bdf, for example from the otf2bdf package
#   - bdfconv, from the upstream U8g2 tools/font/bdfconv build
#
# Usage:
#   SYMBOL=u8g2_font_my_font_18_tf OUT=src/fonts/MyFontU8g2.h \
#     tools/convert_ttf_to_u8g2.sh /path/to/MyFont-Regular.ttf
#
# Common environment overrides:
#   SIZE=18
#   MAP=32-126,8211,8212,8216,8217,8220,8221,8226,8230
#   OUT=src/fonts/GeneratedFontU8g2.h
#   SYMBOL=u8g2_font_rsvpnano_generated_18_tf
#   FONT_LABEL='My Font Regular'
#   FONT_LICENSE_NOTE='My Font is licensed under ...'
#   BDFCONV=/path/to/bdfconv
#   OTF2BDF=/path/to/otf2bdf

FONT_PATH=${1:-${FONT_TTF:-}}
SIZE=${SIZE:-18}
MAP=${MAP:-32-126}
OUT=${OUT:-src/fonts/GeneratedFontU8g2.h}
SYMBOL=${SYMBOL:-u8g2_font_rsvpnano_generated_18_tf}
FONT_LABEL=${FONT_LABEL:-$(basename "${FONT_PATH:-font}")}
FONT_LICENSE_NOTE=${FONT_LICENSE_NOTE:-}
BDFCONV=${BDFCONV:-bdfconv}
OTF2BDF=${OTF2BDF:-otf2bdf}
BDFCONV_FORMAT=${BDFCONV_FORMAT:-1}
TMP_ROOT=${TMP_ROOT:-.tmp/fonts}

if [[ -z "${FONT_PATH}" ]]; then
  echo "error: pass /path/to/font.ttf or set FONT_TTF" >&2
  exit 2
fi

if [[ ! -f "${FONT_PATH}" ]]; then
  echo "error: font file not found: ${FONT_PATH}" >&2
  exit 2
fi

if [[ -z "${SYMBOL}" ]]; then
  echo "error: set SYMBOL to the exported U8g2 font symbol" >&2
  exit 2
fi

if [[ -z "${OUT}" ]]; then
  echo "error: set OUT to the generated header path" >&2
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

mkdir -p "$(dirname "${OUT}")" "${TMP_ROOT}"

SAFE_SYMBOL=$(printf '%s' "${SYMBOL}" | tr -c 'A-Za-z0-9_' '_')
BDF="${TMP_ROOT}/${SAFE_SYMBOL}-${SIZE}.bdf"
TMP_OUT="${TMP_ROOT}/${SAFE_SYMBOL}.generated.h"

"${OTF2BDF}" -p "${SIZE}" -r 72 -o "${BDF}" "${FONT_PATH}"
"${BDFCONV}" -f "${BDFCONV_FORMAT}" -m "${MAP}" -n "${SYMBOL}" "${BDF}" -o "${TMP_OUT}"

{
  echo '#pragma once'
  echo
  echo '#include <stdint.h>'
  echo
  echo "// Generated from ${FONT_LABEL} for RSVP Nano."
  echo "// Source: ${FONT_PATH}"
  echo "// Size: ${SIZE}px"
  echo "// U8g2 map: ${MAP}"
  if [[ -n "${FONT_LICENSE_NOTE}" ]]; then
    echo "// ${FONT_LICENSE_NOTE}"
  fi
  echo
  sed '/^#include/d' "${TMP_OUT}"
} > "${OUT}"

echo "wrote ${OUT}"
