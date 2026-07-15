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
    inline constexpr std::string_view kDefaultTypefaceId = "literata";
    constexpr size_t kColorRoleCount = 16;

    enum ColorRole : size_t {
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
        std::string typeface = std::string{kDefaultTypefaceId};
        bool builtIn = false;
    };

    constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
        return ((red & 0xF8U) << 8) | ((green & 0xFCU) << 3) | (blue >> 3);
    }

    std::string_view colorRoleName(size_t role);
    size_t colorRoleIndexForName(std::string_view name);
    Theme defaultTheme();
    bool hasThemeExtension(std::string_view path);
    std::string themeIdFromPath(std::string_view path);
    bool parseThemeText(std::string_view text, std::string_view id, Theme& theme, std::string& error,
                        bool* hasTypefaceValue = nullptr);

} // namespace ui::themes
