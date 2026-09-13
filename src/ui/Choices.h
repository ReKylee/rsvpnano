#pragma once

#include <array>
#include <cstddef>
#include <span>
#include "ui/Localization.h"

namespace ui {
    template<typename T>
    struct Choice {
        T value;
        UiText label;
    };

    template<typename T, size_t N>
    constexpr size_t choiceIndex(const T& value, std::span<const Choice<T>, N> choices) {
        for (size_t index = 0; index < choices.size(); ++index)
            if (choices[index].value == value)
                return index;
        return choices.size();
    }

    template<typename T, size_t N>
    constexpr bool cycleChoice(T& value, std::span<const Choice<T>, N> choices) {
        if (choices.empty())
            return false;
        const size_t index = choiceIndex(value, choices);
        const T next = choices[index == choices.size() ? 0 : (index + 1) % choices.size()].value;
        if (value == next)
            return false;
        value = next;
        return true;
    }
} // namespace ui
