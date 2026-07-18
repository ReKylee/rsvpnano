# RSVP Nano preview-v0.0.9

Preview firmware for the v0.0.9 cycle. This build starts from v0.0.8 and adds the
current main branch hardware/platform work, durable progress storage, companion app location
editing, and OTA release-channel cleanup.

## Baseline From v0.0.8

- Reader touch gestures were tightened so top and bottom edge menus are harder to open by accident
  while adjusting WPM.
- Swipe-right Back navigation works through menus and text entry.
- Reader controls can move rewind to the top-right for one-handed use.
- RSS parsing accepts larger feeds and can keep usable complete items from partial feed downloads.

## Changes Since v0.0.8

- Added Waveshare ESP32-C6 Touch LCD 1.47 firmware support.
- Added AMOLED board variants and documented the expanded multi-board firmware layout.
- Moved board-specific display, input, storage, power, audio, and system wiring behind per-platform
  APIs.
- Added benchmark firmware mode and helper scripts for firmware performance checks.
- Fixed rotated touch mapping and CST92xx monitor-mode configuration.
- Fixed AMOLED 1.8 v2 touch/display bring-up issues: CST816 touch, faster touch polling, shorter
  button debounce, delayed panel power, deferred display-on, boot-held shutdown filtering, and a
  16 px column offset.
- Cleaned OTA assets so releases use board-specific OTA binaries only.
- Added `github_tag` OTA channel pinning from `/config/ota.conf` and the device Wi-Fi settings menu.
- Added hidden `.rstate.toml` book-state sidecars for durable reading progress and optional per-book
  typography. Stale state is ignored when the source book changes.
- Replaced per-setting persistence with versionless Glaze TOML mirrored between `/config/settings.toml`
  and one NVS blob. Missing fields receive defaults, unknown fields are removed on canonical rewrite,
  and Wi-Fi passwords remain in a separate NVS-only secrets document.
- Converted themes, RSS feeds, and focus timers to versionless Glaze TOML.
- Exposes opaque book IDs, source size, source fingerprint, word count, exact saved word index, and
  chapter markers from the companion book API.
- Added `PATCH /api/books/position` so companion apps can set saved book progress without relying on
  filenames as identifiers.
- Added native companion app controls for choosing a saved book location by chapter, slider, or exact
  word number.
- Renamed the Touch LCD 3.49 full browser-flasher images to board-specific asset names.

## Assets

- `rsvp-nano-esp32-s3-touch-lcd-3.49.bin` is the full browser-flasher image for Touch LCD 3.49.
- `rsvp-nano-esp32-s3-touch-lcd-3.49-ota.bin` is the Touch LCD 3.49 OTA binary.
- `rsvp-nano-esp32-s3-touch-lcd-3.49-rev2.bin` is the full browser-flasher image for Touch LCD
  3.49 rev2/GPIO42 boards.
- `rsvp-nano-esp32-s3-touch-lcd-3.49-rev2-ota.bin` is the Touch LCD 3.49 rev2/GPIO42 OTA binary.
- `rsvp-nano-esp32-s3-touch-amoled-1.8.bin` is the full browser-flasher image for Touch AMOLED 1.8
  V1 boards.
- `rsvp-nano-esp32-s3-touch-amoled-1.8-ota.bin` is the Touch AMOLED 1.8 V1 OTA binary.
- `rsvp-nano-esp32-s3-touch-amoled-1.8-v2.bin` is the full browser-flasher image for Touch AMOLED
  1.8 V2 boards.
- `rsvp-nano-esp32-s3-touch-amoled-1.8-v2-ota.bin` is the Touch AMOLED 1.8 V2 OTA binary.
- `rsvp-nano-esp32-s3-touch-amoled-2.06.bin` is the full browser-flasher image for Touch AMOLED
  2.06 boards.
- `rsvp-nano-esp32-s3-touch-amoled-2.06-ota.bin` is the Touch AMOLED 2.06 OTA binary.
- `rsvp-nano-esp32-s3-touch-amoled-2.16.bin` is the full browser-flasher image for Touch AMOLED
  2.16 boards.
- `rsvp-nano-esp32-s3-touch-amoled-2.16-ota.bin` is the Touch AMOLED 2.16 OTA binary.
- `rsvp-nano-esp32-s3-touch-amoled-2.41.bin` is the full browser-flasher image for Touch AMOLED
  2.41 boards.
- `rsvp-nano-esp32-s3-touch-amoled-2.41-ota.bin` is the Touch AMOLED 2.41 OTA binary.
