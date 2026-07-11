#include "ui/screens/ScreenCommon.h"

namespace screens {

    Action sync(ui::Context& ui, Screen& screen) {
        if (const Action action = detail::navigation(ui, Screen::Sync, screen); action != Action::None)
            return action;
        ui::Column column{detail::tabContent(ui), 6};
        if (ui.button(column.next(26), "Back"))
            screen = Screen::Device;
        if (ui.button(column.next(32), "Companion Sync"))
            return Action::CompanionSync;
        if (ui.button(column.next(32), "Refresh RSS"))
            return Action::RssRefresh;
        if (ui.button(column.next(32), "USB Transfer"))
            return Action::UsbTransfer;
        return Action::None;
    }

} // namespace screens
