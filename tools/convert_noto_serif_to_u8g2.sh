#!/usr/bin/env bash
set -euo pipefail

# Noto Serif preset for the generic RSVP Nano TTF -> U8g2 converter.
#
# Usage:
#   tools/convert_noto_serif_to_u8g2.sh /path/to/NotoSerif-Regular.ttf
#
# Or:
#   NOTO_SERIF_TTF=/path/to/NotoSerif-Regular.ttf tools/convert_noto_serif_to_u8g2.sh

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
FONT_PATH=${1:-${NOTO_SERIF_TTF:-}}

if [[ -z "${FONT_PATH}" ]]; then
  echo "error: pass /path/to/NotoSerif-Regular.ttf or set NOTO_SERIF_TTF" >&2
  exit 2
fi

# Keep coverage compact for firmware flash while covering normal English books.
# 32-126: printable Basic Latin
# 8211/8212: en/em dash
# 8216/8217/8220/8221: curly quotes
# 8226: bullet
# 8230: ellipsis
export SIZE=${SIZE:-18}
export MAP=${MAP:-32-126,8211,8212,8216,8217,8220,8221,8226,8230}
export OUT=${OUT:-src/fonts/NotoSerifU8g2.h}
export SYMBOL=${SYMBOL:-u8g2_font_rsvpnano_noto_serif_18_tf}
export FONT_LABEL=${FONT_LABEL:-Noto Serif Regular}
export FONT_LICENSE_NOTE=${FONT_LICENSE_NOTE:-Noto Serif is licensed under the SIL Open Font License 1.1.}

exec "${SCRIPT_DIR}/convert_ttf_to_u8g2.sh" "${FONT_PATH}"
