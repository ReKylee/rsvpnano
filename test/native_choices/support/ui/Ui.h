#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include "ui/Localization.h"

namespace ui {
    struct Rect { int16_t x = 0, y = 0, w = 0, h = 0; };
    struct Grid {
        Rect bounds;
        uint8_t columns = 1;
        int16_t rowHeight = 0;
        int16_t gap = 0;
        uint16_t index = 0;
        constexpr Rect next() {
            const auto count = std::max<uint8_t>(1, columns);
            const int16_t width = (bounds.w - gap * (count - 1)) / count;
            const auto column = index % count;
            const auto row = index++ / count;
            return {static_cast<int16_t>(bounds.x + column * (width + gap)),
                    static_cast<int16_t>(bounds.y + row * (rowHeight + gap)), width, rowHeight};
        }
    };
    enum class SettingLayout { Inline, Stacked };
    class Context {
    public:
        bool activated = false;
        std::string buttonToActivate;
        int translations = 0;
        int settings = 0;
        int cards = 0;
        int numerics = 0;
        int lastMinimum = 0, lastMaximum = 0, lastStep = 0;
        SettingLayout layout = SettingLayout::Inline;
        uint8_t cardSize = 0;
        UiText lastText = UiText::Unknown;
        std::string_view text(UiText key) { ++translations; lastText = key; return "caption"; }
        bool setting(Rect, std::string_view, std::string_view, SettingLayout mode) {
            ++settings; layout = mode; return activated;
        }
        bool card(Rect, std::string_view, std::string_view, uint8_t size) {
            ++cards; cardSize = size; return activated;
        }
        template<typename T>
        bool slider(Rect, std::string_view, T&, std::string_view) { ++numerics; return false; }
        template<typename T>
        bool stepper(Rect, std::string_view, T&, std::string_view) { ++numerics; return false; }
        bool rotary(Rect, int&, int minimum, int maximum, int step, std::string_view) {
            ++numerics; lastMinimum = minimum; lastMaximum = maximum; lastStep = step; return false;
        }
        bool button(Rect, std::string_view label) { return label == buttonToActivate; }
    };
}
