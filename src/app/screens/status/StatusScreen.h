#pragma once

#include "ui/Ui.h"

namespace screens {

    void status(ui::Context& ui, std::string_view title, std::string_view line1 = {}, std::string_view line2 = {},
                int progress = -1);

} // namespace screens
