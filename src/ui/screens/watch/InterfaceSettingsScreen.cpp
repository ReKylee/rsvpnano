#include "ui/screens/InterfaceSettingsLayout.h"
#include "ui/screens/watch/Layout.h"

namespace screens::interfaceLayout {
    Layout make(ui::Context& ui, Screen& screen) {
        const auto area = watch::header(ui, ui.text(UiText::Display), Screen::Settings, screen);
        const auto grid = ui.pagedGrid(area, fieldCount, 1, 56);
        Layout layout;
        layout.style = {ui::ValueLayout::Card, watch::textSize(ui)};
        for (size_t index = 0; index < fieldCount; ++index)
            layout.fields[index] = grid.item(index);
        return layout;
    }

    bool brightness(ui::Context& ui, ui::Rect rect, settings::InterfaceSettings& config) {
        if (rect.w <= 0 || rect.h <= 0)
            return false;
        return ui.stepper(rect, ui.text(UiText::Brightness), config.brightnessPercent, "%");
    }
} // namespace screens::interfaceLayout
