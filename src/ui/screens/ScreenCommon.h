#pragma once

#include "ui/screens/Screens.h"

namespace screens::detail {

    inline constexpr int16_t kBackButtonHeight = 30;

    Action navigation(ui::Context& ui, Screen active, Screen& screen);
    ui::Rect content(ui::Context& ui);
    ui::Rect tabContent(ui::Context& ui);

} // namespace screens::detail
