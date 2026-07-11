#pragma once

#include <Arduino.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace ui::themes {

    constexpr const char* kDefaultThemeId = "default";
    constexpr const char* kThemeMagic = "@rtheme";
    constexpr const char* kThemeExtension = ".rtheme";
    constexpr size_t kColorRoleCount = 16;

    enum class ReaderTypeface : uint8_t {
        Standard = 0,
        OpenDyslexic = 1,
        AtkinsonHyperlegible = 2,
    };

    enum class ColorRole : uint8_t {
        Background = 0,
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

    struct Theme {
        String id;
        String name;
        std::array<uint16_t, kColorRoleCount> colors = {};
        ReaderTypeface typeface = ReaderTypeface::Standard;
        bool builtIn = false;
        bool lowBrightness = false;
    };

    constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
        return static_cast<uint16_t>(((red & 0xF8U) << 8) | ((green & 0xFCU) << 3) | (blue >> 3));
    }

    const char* colorRoleName(ColorRole role);
    int colorRoleIndexForName(const String& name);
    const char* readerTypefaceName(ReaderTypeface typeface);
    bool readerTypefaceForName(const String& name, ReaderTypeface& typeface);
    Theme defaultTheme();
    bool hasThemeExtension(const String& path);
    String themeIdFromPath(const String& path);
    bool parseThemeText(const String& text, const String& id, Theme& theme, String& error);

} // namespace ui::themes
