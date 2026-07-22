#include "ui/screens/ScreenCommon.h"

namespace screens {

    Action sync(ui::Context& ui, Screen& screen) {
        if (const Action action = detail::navigation(ui, Screen::Sync, screen); action != Action::None)
            return action;
        ui::Column column{detail::tabContent(ui), 6};
        if (ui.button(column.next(detail::kBackButtonHeight), "<<"))
            screen = Screen::Device;
        if (ui.button(column.next(32), ui.text(UiText::CompanionSync)))
            return Action::CompanionSync;
        if (ui.button(column.next(32), ui.text(UiText::RefreshRss)))
            return Action::RssRefresh;
        if (ui.button(column.next(32), ui.text(UiText::UsbTransfer)))
            return Action::UsbTransfer;
        return Action::None;
    }

} // namespace screens
