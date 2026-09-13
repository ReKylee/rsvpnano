#pragma once
#include <algorithm>
#include <array>
#include <vector>
#include "ui/Touch.h"
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
    constexpr bool contains(Rect rect, uint16_t x, uint16_t y) {
        return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
    }
    namespace themes {
        enum ColorRole { Foreground, Muted };
        struct Theme {
            std::string id;
            struct Definition { std::string name; } definition;
        };
    }
    enum class TextAlign { Start, Center };
    struct PagedGrid;
    enum class SettingLayout { Inline, Stacked };
    class Context {
    public:
        struct Control { UiText label; Rect rect; std::string value; };
        std::vector<Control> controls;
        std::array<int, static_cast<size_t>(UiText::Count)> translated{};
        UiText activateField = UiText::Count;
        int numericDelta = 0;
        int16_t surfaceWidth = 640, surfaceHeight = 172;
        size_t page = 0;
        int themeApplications = 0, localeApplications = 0;
        std::string selectedTheme, selectedLocale;
        int16_t width() const { return surfaceWidth; }
        int16_t height() const { return surfaceHeight; }
        PagedGrid pagedGrid(Rect rect, size_t count, uint8_t columns, int16_t minimumHeight);
        void setTheme(const themes::Theme& theme) { ++themeApplications; selectedTheme = theme.id; }
        void setLocale(std::string_view locale) { ++localeApplications; selectedLocale = locale; }
        void label(Rect, std::string_view, uint8_t, themes::ColorRole, TextAlign) {}
        void separator(Rect, std::string_view) {}
        bool activated = false;
        std::string buttonToActivate;
        int translations = 0;
        int settings = 0;
        int cards = 0;
        int numerics = 0;
        int sliders = 0;
        int steppers = 0;
        std::string lastValue;
        int lastMinimum = 0, lastMaximum = 0, lastStep = 0;
        SettingLayout layout = SettingLayout::Inline;
        uint8_t cardSize = 0;
        UiText lastText = UiText::Unknown;
        std::string_view text(UiText key) {
            ++translations; ++translated[static_cast<size_t>(key)]; lastText = key;
            return key == UiText::Off ? "off" : "caption";
        }
        bool setting(Rect rect, std::string_view, std::string_view value, SettingLayout mode) {
            ++settings; layout = mode; lastValue = value;
            controls.push_back({lastText, rect, std::string{value}});
            return activated || activateField == lastText;
        }
        bool card(Rect rect, std::string_view, std::string_view value, uint8_t size) {
            ++cards; cardSize = size; lastValue = value;
            controls.push_back({lastText, rect, std::string{value}});
            return activated || activateField == lastText;
        }
        template<typename T>
        bool slider(Rect rect, std::string_view, T& value, std::string_view) {
            ++numerics; ++sliders; return editNumber(rect, value);
        }
        template<typename T>
        bool stepper(Rect rect, std::string_view, T& value, std::string_view) {
            ++numerics; ++steppers; return editNumber(rect, value);
        }
        template<typename T>
        bool editNumber(Rect rect, T& value) {
            controls.push_back({lastText, rect, {}});
            if (activateField != lastText)
                return false;
            const int before = static_cast<int>(value);
            value = before + numericDelta;
            return static_cast<int>(value) != before;
        }
        bool rotary(Rect, int&, int minimum, int maximum, int step, std::string_view) {
            ++numerics; lastMinimum = minimum; lastMaximum = maximum; lastStep = step; return false;
        }
        bool button(Rect, std::string_view label) { return label == buttonToActivate; }
    };
}
