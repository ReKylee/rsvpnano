#pragma once
#include "ui/Ui.h"
#include "ui/screens/Screen.h"
#include "ui/Layouts.h"
#include "settings/SettingsModel.h"
namespace screens {
    Action settings(ui::Context&, Screen&);
    bool readingSettings(ui::Context&, settings::ReadingSettings&, Screen&);
}
