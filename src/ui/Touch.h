#pragma once

#include <cstdint>

namespace ui {

    enum class Orientation : uint8_t {
        Portrait = 0,
        LandscapeFlipped = 1,
        PortraitFlipped = 2,
        Landscape = 3,
    };

    constexpr Orientation opposite(Orientation orientation) {
        return static_cast<Orientation>((static_cast<uint8_t>(orientation) + 2U) & 3U);
    }

    enum TouchAction : uint8_t {
        TouchNone = 0,
        TouchStart = 1U << 0,
        TouchMove = 1U << 1,
        TouchRelease = 1U << 2,
        TouchTap = 1U << 3,
        TouchHold = 1U << 4,
    };

    struct Touch {
        uint8_t actions = TouchNone;
        uint16_t x = 0;
        uint16_t y = 0;
    };

    constexpr bool hasTouch(const Touch& touch, TouchAction action) {
        return (touch.actions & action) != 0;
    }

    struct TouchContact {
        bool touched = false;
        uint16_t x = 0;
        uint16_t y = 0;
    };

    struct TouchSurface {
        uint16_t width = 0;
        uint16_t height = 0;
    };

    struct TouchTiming {
        uint8_t releaseConfirmSamples = 2;
        uint8_t maxConsecutiveReadFailures = 5;
        uint16_t tapMoveTolerancePx = 20;
        uint16_t tapMaxDurationMs = 300;
        uint16_t holdMs = 420;
        uint32_t pollIntervalMs = 20;
        uint32_t failureBackoffMs = 250;
        uint32_t recoveryRetryMs = 1000;
        uint32_t recoveryEventIgnoreMs = 0;
    };

    struct TouchSource {
        TouchSurface surface;
        TouchTiming timing;
        bool (*begin)() = nullptr;
        bool (*ready)() = nullptr;
        bool (*read)(TouchContact&) = nullptr;
        Orientation (*orientation)() = nullptr;
    };

} // namespace ui
