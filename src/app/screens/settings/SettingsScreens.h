#pragma once

#include "app/screens/Navigation.h"
#include "ui/Ui.h"
#include "settings/SettingsModel.h"

namespace screens {

    Action settings(ui::Context& ui, Screen& screen);
    bool readingSettings(ui::Context& ui, settings::ReadingSettings& settings, Screen& screen);
    bool pacingSettings(ui::Context& ui, settings::PacingSettings& settings, Screen& screen);

} // namespace screens
