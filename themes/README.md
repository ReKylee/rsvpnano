# RSVP Nano Themes

Copy this `themes` folder to the SD card root. The device loads `*.rtheme` files from `/themes`
at startup and appends valid themes after the built-in default theme.

Theme files are plain text:

```ini
@rtheme
name=Catppuccin Mocha
typeface=literata

background=#1e1e2e
foreground=#cdd6f4
muted=#a6adc8
subtle=#7f849c
accent=#f38ba8
accent_bar=#f38ba8
break_accent=#a6e3a1
on_accent=#11111b
surface=#181825
surface_muted=#313244
surface_active=#45475a
outline=#585b70
guide=#6c7086
guide_focus=#f38ba8
phantom=#6c7086
progress_track=#313244
```

Required color roles are RGB values written as `#RRGGBB`. `typeface` is a font catalog family ID;
missing or invalid values are replaced with the embedded `literata` font.
