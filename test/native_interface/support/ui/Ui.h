#pragma once
#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>
#include "ui/Geometry.h"
#include "ui/Localization.h"
#include "ui/Touch.h"

namespace ui {
    namespace themes {
        enum ColorRole { Foreground, Muted };
        struct Theme {
            std::string id;
            struct Definition { std::string name; } definition;
        };
    }
    enum class TextAlign { Start, Center };
    enum class SettingLayout { Inline, Stacked };
    struct PagedGrid;

    // Interface behavior fixture, not an implementation of production Context.
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
        int sliders = 0, steppers = 0;
        std::string selectedTheme, selectedLocale, buttonToActivate;
        int16_t width() const { return surfaceWidth; }
        int16_t height() const { return surfaceHeight; }
        PagedGrid pagedGrid(Rect rect, size_t count, uint8_t columns, int16_t minimumHeight);
        void setTheme(const themes::Theme& theme) { ++themeApplications; selectedTheme = theme.id; }
        void setLocale(std::string_view locale) { ++localeApplications; selectedLocale = locale; }
        void label(Rect, std::string_view, uint8_t, themes::ColorRole, TextAlign) {}
        void separator(Rect, std::string_view) {}
        std::string_view text(UiText key) {
            ++translated[static_cast<size_t>(key)];
            return captions()[static_cast<size_t>(key)];
        }
        bool setting(Rect rect, std::string_view label, std::string_view value, SettingLayout) {
            return record(rect, label, value);
        }
        bool card(Rect rect, std::string_view label, std::string_view value, uint8_t) {
            return record(rect, label, value);
        }
        template<typename T>
        bool slider(Rect rect, std::string_view label, T& value, std::string_view) {
            ++sliders; return editNumber(rect, label, value);
        }
        template<typename T>
        bool stepper(Rect rect, std::string_view label, T& value, std::string_view) {
            ++steppers; return editNumber(rect, label, value);
        }
        bool rotary(Rect, int&, int, int, int, std::string_view) { return false; }
        bool button(Rect, std::string_view label) { return label == buttonToActivate; }

    private:
        static const std::array<std::string, static_cast<size_t>(UiText::Count)>& captions() {
            static const auto result = [] {
                std::array<std::string, static_cast<size_t>(UiText::Count)> values;
                for (size_t i = 0; i < values.size(); ++i)
                    values[i] = "caption-" + std::to_string(i);
                values[static_cast<size_t>(UiText::Off)] = "off";
                return values;
            }();
            return result;
        }
        bool record(Rect rect, std::string_view label, std::string_view value) {
            const auto& labels = captions();
            const auto found = std::ranges::find(labels, label);
            const auto key = static_cast<UiText>(found - labels.begin());
            controls.push_back({key, rect, std::string{value}});
            return key != UiText::Count && activateField == key;
        }
        template<typename T>
        bool editNumber(Rect rect, std::string_view label, T& value) {
            if (!record(rect, label, {}))
                return false;
            const int before = static_cast<int>(value);
            value = before + numericDelta;
            return static_cast<int>(value) != before;
        }
    };
}
