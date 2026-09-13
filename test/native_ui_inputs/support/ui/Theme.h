#pragma once
#include <cstdint>
namespace ui::themes {
    struct Theme {};
    enum ColorRole : uint8_t {
        Background, Foreground, Muted, Subtle, Accent, AccentBar, BreakAccent, OnAccent,
        Surface, SurfaceMuted, SurfaceActive, Outline, Guide, GuideFocus, Phantom, ProgressTrack,
    };
    constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
        return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
    }
}
