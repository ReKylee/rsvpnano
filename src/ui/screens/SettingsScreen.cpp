#include "ui/screens/ScreenCommon.h"

namespace screens {

Action settings(ui::Context& ui, Screen& screen) {
    if (const Action action = detail::navigation(ui, Screen::Settings, screen); action != Action::None) {
        return action;
    }
    ui::Grid grid{detail::content(ui), static_cast<uint8_t>(ui.width() >= 400 ? 2 : 1), 48, 10};
    if (ui.button(grid.next(), "Reading")) screen = Screen::ReadingSettings;
    if (ui.button(grid.next(), "Display")) screen = Screen::DisplaySettings;
    if (ui.button(grid.next(), "Pacing")) screen = Screen::PacingSettings;
    if (ui.button(grid.next(), "Typography")) screen = Screen::TypographySettings;
    return Action::None;
}

} // namespace screens
