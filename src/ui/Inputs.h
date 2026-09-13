#pragma once

#include <algorithm>
#include <concepts>
#include <functional>
#include <ranges>
#include <string_view>

#include "ui/Ui.h"

namespace ui {
    enum class ValueLayout : uint8_t { Inline, Stacked, Card };

    struct ValueStyle {
        ValueLayout layout = ValueLayout::Inline;
        uint8_t textSize = 3;
    };

    namespace detail {
        inline std::string_view caption(Context& ui, UiText key) {
            return ui.text(key);
        }

        inline std::string_view caption(Context&, std::string_view text) {
            return text;
        }
    } // namespace detail

    // A labelled value action, optionally computed lazily after the visibility check.
    template<typename Caption, typename Value>
    bool valueButton(Context& ui, Rect rect, const Caption& label, Value&& value, ValueStyle style = {}) {
        if (rect.w <= 0 || rect.h <= 0)
            return false;
        const auto draw = [&](auto&& contents) {
            const auto title = detail::caption(ui, label);
            const auto text = detail::caption(ui, contents);
            return style.layout == ValueLayout::Card
                     ? ui.card(rect, title, text, style.textSize)
                     : ui.setting(rect, title, text, style.layout == ValueLayout::Inline
                                                         ? SettingLayout::Inline : SettingLayout::Stacked);
        };
        if constexpr (std::invocable<Value&>)
            return draw(std::invoke(value));
        else
            return draw(value);
    }

    enum class NumberInput : uint8_t { Slider, Stepper };

    template<typename Caption>
    bool number(Context& ui, Rect rect, const Caption& label, int& value, int minimum, int maximum,
                int step = 1, std::string_view suffix = {}, NumberInput input = NumberInput::Slider) {
        if (rect.w <= 0 || rect.h <= 0 || minimum > maximum || step <= 0)
            return false;
        const int before = value;
        const auto title = detail::caption(ui, label);
        if (input == NumberInput::Stepper)
            ui.stepper(rect, title, value, minimum, maximum, step, suffix);
        else
            ui.slider(rect, title, value, minimum, maximum, step, suffix);
        return value != before;
    }

    template<typename Caption, typename T>
        requires requires { T::min(); T::max(); T::step(); }
    bool number(Context& ui, Rect rect, const Caption& label, T& value, std::string_view suffix = {},
                NumberInput input = NumberInput::Slider) {
        const int before = static_cast<int>(value);
        int scalar = before;
        if (!number(ui, rect, label, scalar, T::min(), T::max(), T::step(), suffix, input))
            return false;
        value = scalar;
        return static_cast<int>(value) != before;
    }

    // Tap to advance through an existing range. Labels and keys are synchronous projections;
    // nothing is retained, and callers need not manufacture a separate option model.
    template<typename Caption, typename Value, std::ranges::forward_range Items,
             typename LabelFor = std::identity, typename ValueFor = std::identity>
    bool select(Context& ui, Rect rect, const Caption& label, Value& value, Items&& items,
                LabelFor labelFor = {}, ValueStyle style = {}, ValueFor valueFor = {}) {
        if (rect.w <= 0 || rect.h <= 0)
            return false;
        auto first = std::ranges::begin(items);
        const auto end = std::ranges::end(items);
        if (first == end)
            return false;
        const auto current = std::ranges::find_if(items, [&](auto&& item) {
            return std::invoke(valueFor, item) == value;
        });
        // A projected temporary is consumed during valueButton's synchronous call.
        const bool activated = current == end
                                 ? valueButton(ui, rect, label, UiText::Unknown, style)
                                 : valueButton(ui, rect, label, std::invoke(labelFor, *current), style);
        if (!activated)
            return false;
        auto next = current;
        if (next == end || ++next == end)
            next = first;
        decltype(auto) nextValue = std::invoke(valueFor, *next);
        if (value == nextValue)
            return false;
        // Assignment can normalize a bounded value. Snapshot only after an actual activation.
        const Value before = value;
        value = nextValue;
        return !(value == before);
    }

    // Compose the existing dial and buttons without a form object or persistence policy.
    inline bool rotaryStepper(Context& ui, Rect rect, std::string_view label, int& value,
                              int minimum, int maximum, int step) {
        if (rect.w < 3 || rect.h <= 0 || maximum <= minimum || step <= 0)
            return false;
        const int before = value;
        const int16_t diameter = std::min<int16_t>(rect.h, rect.w / 2);
        const int16_t sideWidth = std::min<int16_t>(48, (rect.w - diameter) / 2);
        const Rect dial{static_cast<int16_t>(rect.x + (rect.w - diameter) / 2), rect.y, diameter, rect.h};
        // Apply side-button edits before the dial paints its value in this frame.
        if (ui.button({rect.x, rect.y, sideWidth, rect.h}, "-"))
            value = static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(value) - step, minimum, maximum));
        if (ui.button({static_cast<int16_t>(rect.x + rect.w - sideWidth), rect.y, sideWidth, rect.h}, "+"))
            value = static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(value) + step, minimum, maximum));
        ui.rotary(dial, value, minimum, maximum, step, label);
        return value != before;
    }

    template<typename T>
        requires requires { T::min(); T::max(); T::step(); }
    bool rotaryStepper(Context& ui, Rect rect, std::string_view label, T& value) {
        const int before = static_cast<int>(value);
        int scalar = before;
        if (!rotaryStepper(ui, rect, label, scalar, T::min(), T::max(), T::step()))
            return false;
        value = scalar;
        return static_cast<int>(value) != before;
    }
} // namespace ui
