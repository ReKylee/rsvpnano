#pragma once

#include "app/screens/Navigation.h"
#include "ui/Ui.h"

namespace screens {

    Action read(ui::Context& ui, std::string_view title, std::string_view author, uint8_t progress, Screen& screen);

} // namespace screens
