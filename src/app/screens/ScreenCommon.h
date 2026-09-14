#pragma once

#include "app/screens/Navigation.h"
#include "ui/Ui.h"

namespace screens::detail {

    inline constexpr int16_t kBackButtonHeight = 36;

    Action navigation(ui::Context& ui, Screen active, Screen& screen);
    ui::Rect content(ui::Context& ui);
    ui::Rect tabContent(ui::Context& ui);

} // namespace screens::detail
