# Device UI layouts

Each board's `build_src_filter` selects exactly one presentation:

| Boards | Screen implementation |
| --- | --- |
| LCD 3.49 rev1/rev2 | `src/app/screens/<domain>/regular/` |
| AMOLED 2.41, 2.16, 2.06, 1.8 v1/v2; LCD 1.47 | `src/app/screens/<domain>/watch/` |

`RSVP_UI_WATCH` selects the matching interaction state at compile time; there is
no runtime layout switch. The regular source filter excludes `watch/`; the watch
source filter excludes `regular/`. Both implement the same domain screen contracts.
Do not include one presentation's implementation from another.

## Ownership

- `src/ui/`: shared rendering, touch handling, cards, rings, rotary controls,
  pagination, keyboard, typography, and theme primitives.
- `src/app/screens/<domain>/regular/` and `watch/`: screen arrangement and presentation.
- `src/app/screens/reader/ReaderLayout.h`: the selected reader presentation's geometry,
  page-font strike, and chrome contract. The shared reading engine calls it without
  inspecting the device or UI kind.
- Shared screen workflow files retain book metadata, network operations, timer
  persistence, and reading state. Existing settings and focus domain types remain
  the source of truth; layouts do not maintain alternative settings or timer lists.

The watch presentation derives bounds from the actual display dimensions. At
320x172, settings and Device actions page through large rows/pairs. USB transfer
and Companion sync occupy the first Device page. Larger displays show more items
at once. Paging does not create hit targets for hidden items.

Chapter and timer carousels only draw three visible cards. Tap a side to select it;
tap the center to activate it. Horizontal swipes select without activating. The WPM
ring uses relative horizontal drag and retains explicit minus/plus controls.

## Native watch orientation

Watch menus use the panel's natural portrait axes: 450x600 (2.41), 480x480 (2.16),
410x502 (2.06), 368x448 (1.8 V1/V2), and 172x320 (LCD 1.47). Each board declares
`Portrait` as its default orientation and uses native width/height in `BoardConfig`.
LCD 3.49 keeps its existing landscape orientation and regular presentation.

Draw directly to the watch panel; no additional canvas/framebuffer is required.
SH8601 and CO5300 do not support a hardware X/Y-axis exchange, so requesting a
landscape quarter-turn changes logical bounds without correctly rotating pixels.
See the [CO5300 driver](https://github.com/moononournation/Arduino_GFX/blob/master/src/display/Arduino_CO5300.cpp)
and [SH8601 driver](https://github.com/moononournation/Arduino_GFX/blob/master/src/display/Arduino_SH8601.cpp).
The existing handedness option uses a 180-degree flip, with matching touch mapping;
both portrait offset sets must retain the panel's column/row offsets.

## Validation

Run the two test profiles separately:

```sh
uvx platformio test -e native_test -f test_ui
uvx platformio test -e native_watch_test
uvx platformio run -e waveshare_esp32s3_touch_lcd_349_rev1
uvx platformio run -e waveshare_esp32s3_touch_amoled_206
uvx platformio run -e waveshare_esp32c6_touch_lcd_147
```

The watch host tests cover native portrait bounds and the previous landscape sizes;
paging, dock navigation, relative rotary adjustment, and directional
carousel gestures. Host drawing checks do not prove pixel appearance or touch
quality on hardware.

Before release, check on the physical devices:

- The full portrait screen is visible, text is not mirrored, all four corners and
  dock items respond at their drawn positions, and swipes follow the finger in
  both handedness settings. Cold boot and sleep/wake must restore the display.
- Long/localized titles, settings labels, and chapter names remain readable.
- All setting pages, timer creation/editing, and keyboard input remain accessible.
- Side taps and swipes never start a chapter/timer; center taps do.
- RSVP and page modes, vertical CJK, both handedness settings, battery controls,
  and reading-progress taps retain their behavior.
- Timer orientation/pause/complete behavior, USB/Companion operations, theme
  changes, and repeated navigation leave no stale pixels.
- The 3.49 presentation retains its existing appearance and navigation.

No hardware flashing is part of these host/build checks.

## Source and state ownership

The widget engine is in `src/ui`: frame caching (`Context.cpp`), surface transfers
(`Paint.cpp`), text preparation (`Text.cpp`), touch gestures (`Touch.cpp`), and
widget families. `Geometry.h` and `Layouts.h` do not depend on graphics or application
screens. There is no retained widget tree or per-widget heap state; the existing
64-entry frame cache and bounded display strips remain.

Application screens live in `src/app/screens/<domain>`. Each domain keeps shared
workflows beside `regular` and `watch` presentation implementations. Navigation IDs
are in `Navigation.h`; each stateful screen declares its own state in its domain
header. `RSVP_UI_WATCH` selects only the presentation-specific interaction fields.
The same build configuration selects implementation files. The core application
remains one owner, with its implementations separated into navigation, background
jobs, power, preferences, USB transfer, and the boot/update loop.

Settings menu entries, pacing member references, reading choices, and font target
selection are shared data/operations. Presentations supply geometry and controls,
not copies of settings or parallel action tables. Book font enumeration borrows
metadata and the font catalog; only an actual override edit mutates the owning
settings vector. No serialization, persistence format, hardware driver, dependency,
or toolchain was changed by this reorganization.

`Context::paint` owns the complete rectangular surface and supplies its background
on both direct and strip-buffered displays. Its optional background-color argument
lets full-surface controls avoid painting the same background twice. Prepare text
before entering a paint callback, which may run once per display strip. Use
`redraw(region, signature, true)` before painting an owned region; the default
partial-custom claim still clears before callers draw directly. Keep sibling
regions non-overlapping and submission order stable; the fixed cache is positional,
not an arbitrary overlapping compositor. Cache overflow remains a redraw fallback.

Run `python tools/check_ui_sources.py` after changing PlatformIO filters. It uses
PlatformIO's source matcher and verifies layout/state agreement, optional widget
selection, benchmark entrypoints, and the library-to-application dependency boundary.
`test_ui`, `test_watch_ui`, and `test_amoled_render` exercise cache behavior,
settings actions, layout bounds, touch behavior, text work, and strip transfers.
