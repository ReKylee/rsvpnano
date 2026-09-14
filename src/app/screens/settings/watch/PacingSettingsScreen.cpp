#include "app/screens/settings/PacingChoices.h"
#include "app/screens/settings/SettingsScreens.h"
#include "app/screens/watch/Layout.h"

namespace screens {
    bool pacingSettings(ui::Context& ui, settings::PacingSettings& config, Screen& screen) {
        auto area = watch::header(ui, ui.text(UiText::WordPacing), Screen::Settings, screen);
        auto grid = ui.pagedGrid(area, 4, 1, 60);
        bool changed = false;
        for (size_t index = 0; index < kPacingChoices.size(); ++index) {
            const auto& choice = kPacingChoices[index];
            changed |= watch::stepper(ui, grid.item(index), choice.label, config.*choice.member, " ms");
        }
        if (ui.card(grid.item(3), ui.text(UiText::Reset), {}, watch::textSize(ui))) {
            changed |= config != settings::PacingSettings{};
            config = {};
        }
        return changed;
    }
} // namespace screens
