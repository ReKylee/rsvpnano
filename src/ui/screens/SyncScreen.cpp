#include "ui/screens/ScreenCommon.h"

namespace screens {

    Action sync(ui::Context& ui, Screen& screen) {
        if (const Action action = detail::navigation(ui, Screen::Sync, screen); action != Action::None)
            return action;
        const ui::Rect content = detail::tabContent(ui);
        constexpr int16_t gap = 6;
        const int16_t rowHeight = static_cast<int16_t>((content.h - gap) / 2);
        ui::Grid actions{content, 2, rowHeight, gap};
        if (ui.button(actions.next(), "<<"))
            screen = Screen::Device;
        if (ui.button(actions.next(), ui.text(UiText::CompanionSync)))
            return Action::CompanionSync;
        if (ui.button(actions.next(), ui.text(UiText::RefreshRss)))
            return Action::RssRefresh;
        if (ui.button(actions.next(), ui.text(UiText::UsbTransfer)))
            return Action::UsbTransfer;
        return Action::None;
    }

} // namespace screens
