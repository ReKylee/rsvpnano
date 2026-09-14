#pragma once

#include "app/screens/Navigation.h"
#include "ui/Ui.h"

namespace screens {

    Action ota(ui::Context& ui, std::string_view firmwareVersion, Screen& screen);

} // namespace screens
