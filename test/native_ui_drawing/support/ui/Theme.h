#pragma once
#include <array>
#include <cstdint>
namespace ui::themes {
    enum ColorRole : uint8_t { Background, Foreground, Muted, Accent, Surface, SurfaceMuted,
        SurfaceActive, Outline, ProgressTrack, OnAccent, Count };
    struct Theme { struct { std::array<uint16_t, Count> colors{}; } definition; };
    inline uint16_t color(const std::array<uint16_t, Count>& colors, ColorRole role) { return colors[role]; }
    constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
        return static_cast<uint16_t>((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
    }
}
