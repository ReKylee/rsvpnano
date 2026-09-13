#include "Recording.h"
#include "ui/Layouts.h"
#include <array>

namespace testui {
    std::string_view caption(UiText key) {
        static const auto labels = [] {
            std::array<std::string, static_cast<size_t>(UiText::Count)> result;
            for (size_t i = 0; i < result.size(); ++i)
                result[i] = "text-" + std::to_string(i);
            return result;
        }();
        return labels[static_cast<size_t>(key)];
    }

    bool record(Kind kind, ui::Rect rect, std::string_view label, std::string_view value,
                ui::SettingLayout layout, uint8_t size, bool enabled) {
        recording.calls.push_back({kind, rect, std::string{label}, std::string{value}, layout, size});
        return enabled && rect.w > 0 && rect.h > 0 && !recording.activate.empty()
            && recording.activate == label;
    }
}

// Link the real Context declaration and its template members to a recording backend.
// These tests cover control composition, not pixel rendering, capture, fonts or display transfers.
namespace ui {
    Context::Context(Arduino_GFX& gfx) : gfx_(gfx) {}
    std::string_view Context::text(UiText key) const {
        ++testui::recording.translations;
        return testui::caption(key);
    }
    bool Context::setting(Rect rect, std::string_view label, std::string_view value, SettingLayout layout) {
        return testui::record(testui::Kind::Setting, rect, label, value, layout);
    }
    bool Context::card(Rect rect, std::string_view label, std::string_view value, uint8_t size,
                       themes::ColorRole, Icon, bool enabled, uint8_t) {
        return testui::record(testui::Kind::Card, rect, label, value, SettingLayout::Stacked, size, enabled);
    }
    bool Context::button(Rect rect, std::string_view label, bool enabled, Icon, uint8_t,
                         std::string_view, std::string_view) {
        return testui::record(testui::Kind::Button, rect, label, {}, SettingLayout::Inline, 0, enabled);
    }
    bool Context::iconButton(Rect rect, Icon) { return testui::record(testui::Kind::Button, rect, "icon"); }
    bool Context::tab(Rect rect, std::string_view label, bool, Icon) {
        return testui::record(testui::Kind::Tab, rect, label);
    }
    bool Context::dockItem(Rect rect, std::string_view label, Icon, uint16_t) {
        return testui::record(testui::Kind::Dock, rect, label);
    }
    void Context::label(Rect rect, std::string_view value, uint8_t size, themes::ColorRole, TextAlign,
                        uint8_t, std::string_view, uint8_t) {
        testui::record(testui::Kind::Label, rect, value, {}, SettingLayout::Inline, size);
    }
    void Context::separator(Rect rect, std::string_view value) {
        testui::record(testui::Kind::Separator, rect, value);
    }
    uint16_t Context::color(themes::ColorRole role) const { return static_cast<uint16_t>(role); }
    bool Context::rotary(Rect rect, int& value, int minimum, int maximum, int step, std::string_view label) {
        auto& output = testui::recording;
        output.minimum = minimum; output.maximum = maximum; output.step = step;
        testui::record(testui::Kind::Rotary, rect, label);
        const int before = value;
        if (output.numericValue)
            value = std::clamp(*output.numericValue, minimum, maximum);
        output.renderedNumericValue = value;
        return value != before;
    }
    bool Context::sliderValue(Rect rect, std::string_view label, int& value, int minimum, int maximum, int step,
                              std::string_view, themes::ColorRole) {
        const bool changed = rotary(rect, value, minimum, maximum, step, label);
        testui::recording.calls.back().kind = testui::Kind::Slider;
        return changed;
    }
    bool Context::stepperValue(Rect rect, std::string_view label, int& value, int minimum, int maximum, int step,
                               std::string_view suffix, themes::ColorRole role) {
        const bool changed = sliderValue(rect, label, value, minimum, maximum, step, suffix, role);
        testui::recording.calls.back().kind = testui::Kind::Stepper;
        return changed;
    }
    PagedGrid Context::pagedGrid(Rect rect, size_t count, uint8_t columns, int16_t) {
        // Supply a controlled visible page; production PagedGrid::item performs the actual placement.
        const size_t first = std::min(testui::recording.pageFirst, count);
        const size_t visible = std::min(testui::recording.pageCapacity, count - first);
        const size_t rows = std::max<size_t>(1, (visible + columns - 1) / columns);
        const auto height = static_cast<int16_t>((rect.h - static_cast<int>(rows - 1) * 4) / rows);
        return {rect, first, visible, columns, height, 4};
    }
}
