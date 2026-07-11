#pragma once

#include "ui/screens/Screens.h"

namespace screens::detail {

    Action navigation(ui::Context& ui, Screen active, Screen& screen);
    ui::Rect content(ui::Context& ui);

} // namespace screens::detail
