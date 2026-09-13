#pragma once

#include <algorithm>
#include <cstdint>
#include <string_view>

#include "ui/Choices.h"
#include "ui/Ui.h"

namespace ui {
    enum class SettingPresentation : uint8_t { Inline, Stacked, Card };

    struct SettingsStyle {
        SettingPresentation presentation = SettingPresentation::Inline;
        uint8_t textSize = 3;
    };

    // A synchronous form: owns edit bookkeeping, not the settings or their persistence.
    class SettingsControls {
    public:
        explicit SettingsControls(Context& context, SettingsStyle style = {}) : ui_(context), style_(style) {}

        bool changed() const { return changed_; }

        bool setting(Rect rect, UiText label, std::string_view value) {
            if (rect.w <= 0 || rect.h <= 0)
                return false;
            return style_.presentation == SettingPresentation::Card
                       ? ui_.card(rect, ui_.text(label), value, style_.textSize)
                       : ui_.setting(rect, ui_.text(label), value,
                                     style_.presentation == SettingPresentation::Inline
                                         ? SettingLayout::Inline : SettingLayout::Stacked);
        }

        template<typename T, size_t N>
        bool choice(Rect rect, UiText label, T& value, const std::array<Choice<T>, N>& choices) {
            if (rect.w <= 0 || rect.h <= 0 || choices.empty())
                return false;
            const std::span<const Choice<T>, N> options{choices};
            const size_t index = choiceIndex(value, options);
            const auto caption = index == options.size() ? UiText::Unknown : options[index].label;
            if (!setting(rect, label, ui_.text(caption)))
                return false;
            const bool edited = cycleChoice(value, options);
            changed_ |= edited;
            return edited;
        }

        template<typename T>
        bool slider(Rect rect, UiText label, T& value, std::string_view suffix = {}) {
            if (rect.w <= 0 || rect.h <= 0)
                return false;
            const bool edited = ui_.slider(rect, ui_.text(label), value, suffix);
            changed_ |= edited;
            return edited;
        }

        template<typename T>
        bool stepper(Rect rect, UiText label, T& value, std::string_view suffix = {}) {
            if (rect.w <= 0 || rect.h <= 0)
                return false;
            const bool edited = ui_.stepper(rect, ui_.text(label), value, suffix);
            changed_ |= edited;
            return edited;
        }

        template<typename T>
            requires requires { T::min(); T::max(); T::step(); }
        bool rotary(Rect rect, std::string_view label, T& value) {
            if (rect.w <= 0 || rect.h <= 0)
                return false;
            int scalar = static_cast<int>(value);
            const bool edited = ui_.rotary(rect, scalar, T::min(), T::max(), T::step(), label);
            if (edited)
                value = scalar;
            changed_ |= edited;
            return edited;
        }

        template<typename T>
            requires requires { T::min(); T::max(); T::step(); }
        bool rotaryStepper(Rect rect, std::string_view label, T& value) {
            if (rect.w <= 0 || rect.h <= 0)
                return false;
            const int before = static_cast<int>(value);
            int scalar = before;
            const int16_t size = std::min<int16_t>(rect.h, rect.w / 2);
            const Rect dial{static_cast<int16_t>(rect.x + (rect.w - size) / 2), rect.y, size, rect.h};
            ui_.rotary(dial, scalar, T::min(), T::max(), T::step(), label);
            if (ui_.button({rect.x, rect.y, 48, rect.h}, "-"))
                scalar = std::max<int>(T::min(), scalar - T::step());
            if (ui_.button({static_cast<int16_t>(rect.x + rect.w - 48), rect.y, 48, rect.h}, "+"))
                scalar = std::min<int>(T::max(), scalar + T::step());
            const bool edited = scalar != before;
            if (edited)
                value = scalar;
            changed_ |= edited;
            return edited;
        }

    private:
        Context& ui_;
        SettingsStyle style_;
        bool changed_ = false;
    };
} // namespace ui
