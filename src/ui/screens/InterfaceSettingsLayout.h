#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ui/Inputs.h"

namespace screens {
    enum class Screen : uint8_t;
}

namespace screens::interfaceLayout {
    enum class Field : uint8_t { Brightness, Theme, Language, Standby, Screensaver, Count };
    inline constexpr size_t fieldCount = static_cast<size_t>(Field::Count);

    struct Layout {
        std::array<ui::Rect, fieldCount> fields{};
        ui::ValueStyle style{};
        ui::NumberInput number = ui::NumberInput::Slider;

        ui::Rect& operator[](Field field) { return fields[static_cast<size_t>(field)]; }
        ui::Rect operator[](Field field) const { return fields[static_cast<size_t>(field)]; }
    };

    // Defined by the selected presentation, independent of device/controller selection.
    Layout make(ui::Context& ui, Screen& screen);
} // namespace screens::interfaceLayout
