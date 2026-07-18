#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "settings/SettingsCodec.h"
#include "settings/SettingsModel.h"
#include "ui/Rgb565.h"

namespace ui::themes {

    inline constexpr std::string_view kDefaultThemeId = "default";
    inline constexpr std::string_view kThemeExtension = ".toml";
    inline constexpr std::string_view kDefaultTypefaceId = "literata";

    struct ThemeColors {
        Rgb565 background = 0x0000;
        Rgb565 foreground = 0xFFFF;
        Rgb565 muted = 0x8410;
        Rgb565 subtle = 0x528A;
        Rgb565 accent = 0xF800;
        Rgb565 accentBar = 0xF800;
        Rgb565 breakAccent = rgb565(68, 132, 88);
        Rgb565 onAccent = 0xFFFF;
        Rgb565 surface = 0x0000;
        Rgb565 surfaceMuted = 0x2104;
        Rgb565 surfaceActive = 0x4208;
        Rgb565 outline = 0x8410;
        Rgb565 guide = 0x8410;
        Rgb565 guideFocus = 0xF800;
        Rgb565 phantom = 0x8410;
        Rgb565 progressTrack = 0x8410;

        bool operator==(const ThemeColors&) const = default;
    };

    struct ThemeFile {
        std::string name = "Default";
        settings::TypographySettings typography;
        ThemeColors colors;

        bool operator==(const ThemeFile&) const = default;
    };

    struct ThemeEntry {
        std::string id;
        ThemeFile definition;
        bool builtIn = false;
    };

    using Theme = ThemeEntry;

    enum ColorRole : uint8_t {
        Background,
        Foreground,
        Muted,
        Subtle,
        Accent,
        AccentBar,
        BreakAccent,
        OnAccent,
        Surface,
        SurfaceMuted,
        SurfaceActive,
        Outline,
        Guide,
        GuideFocus,
        Phantom,
        ProgressTrack,
    };

    uint16_t color(const ThemeColors& colors, ColorRole role);
    ThemeEntry defaultTheme(const settings::TypographySettings& typography = {});
    bool hasThemeExtension(std::string_view path);
    std::string themeIdFromPath(std::string_view path);
    settings::SettingsResult<ThemeEntry> decodeToml(std::string_view text, std::string_view id,
                                                    const settings::TypographySettings& defaults = {});
    settings::SettingsResult<std::string> encodeToml(const ThemeFile& theme);

} // namespace ui::themes
