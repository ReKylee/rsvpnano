#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ui::themes {

    inline constexpr std::string_view kDefaultThemeId = "default";
    inline constexpr std::string_view kThemeMagic = "@rtheme";
    inline constexpr std::string_view kThemeExtension = ".rtheme";
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
        std::string id;
        std::string name;
        std::array<uint16_t, kColorRoleCount> colors = {};
        ReaderTypeface typeface = ReaderTypeface::Standard;
        bool builtIn = false;
        bool lowBrightness = false;
    };

    constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
        return static_cast<uint16_t>(((red & 0xF8U) << 8) | ((green & 0xFCU) << 3) | (blue >> 3));
    }

    std::string_view colorRoleName(ColorRole role);
    int colorRoleIndexForName(std::string_view name);
    std::string_view readerTypefaceName(ReaderTypeface typeface);
    bool readerTypefaceForName(std::string_view name, ReaderTypeface& typeface);
    Theme defaultTheme();
    bool hasThemeExtension(std::string_view path);
    std::string themeIdFromPath(std::string_view path);
    bool parseThemeText(std::string_view text, std::string_view id, Theme& theme, std::string& error);

} // namespace ui::themes
