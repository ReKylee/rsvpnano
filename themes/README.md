# RSVP Nano Themes

Copy this `themes` folder to the SD card root. The device loads `*.toml` files from `/themes`
at startup and appends valid themes after the built-in default theme.

Theme files are plain text:

```toml
name = "Catppuccin Mocha"

[typography]
fontId = "literata"
fontSizeIndex = 0
focusHighlight = true
tracking = 0
anchor = 30
guideWidth = 30
guideGap = 5

[colors]
background = "#1E1E2E"
foreground = "#CDD6F4"
muted = "#A6ADC8"
subtle = "#7F849C"
accent = "#F38BA8"
accentBar = "#F38BA8"
breakAccent = "#94E2D5"
onAccent = "#11111B"
surface = "#181825"
surfaceMuted = "#313244"
surfaceActive = "#45475A"
outline = "#585B70"
guide = "#6C7086"
guideFocus = "#F38BA8"
phantom = "#6C7086"
progressTrack = "#313244"
```

Colors are quoted `#RRGGBB` values; without quotes, `#` starts a TOML comment. Missing fields keep
the built-in defaults and unknown fields are ignored. `fontId` is a font catalog family ID;
unavailable fonts fall back through the device font catalog, normally to the embedded `literata`
font. Theme files do not use a schema version.
