#include "ui/screens/InterfaceSettingsLayout.h"
#include "ui/screens/watch/Layout.h"

namespace screens::interfaceLayout {
    Layout make(ui::Context& ui, Screen& screen) {
        const auto area = watch::header(ui, ui.text(UiText::Display), Screen::Settings, screen);
        const auto grid = ui.pagedGrid(area, fieldCount, 1, 56);
        Layout layout;
        layout.style = {ui::SettingPresentation::Card, watch::textSize(ui), ui::NumberPresentation::Stepper};
        for (size_t index = 0; index < fieldCount; ++index)
            layout.fields[index] = grid.item(index);
        return layout;
    }
} // namespace screens::interfaceLayout
