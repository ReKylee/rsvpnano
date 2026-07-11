#include "ui/screens/ScreenCommon.h"

namespace screens {

    Action ota(ui::Context& ui, Screen& screen) {
        if (const Action action = detail::navigation(ui, Screen::Ota, screen); action != Action::None)
            return action;
        ui::Column column{detail::tabContent(ui), 8};
        if (ui.button(column.next(28), ui.text(UiText::Back)))
            screen = Screen::Device;
        if (ui.button(column.next(40), ui.text(UiText::CheckOnly)))
            return Action::OtaCheck;
        if (ui.button(column.next(40), ui.text(UiText::InstallUpdate)))
            return Action::OtaInstall;
        return Action::None;
    }

} // namespace screens
