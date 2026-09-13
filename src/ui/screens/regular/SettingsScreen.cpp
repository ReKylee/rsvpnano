#include "ui/screens/ScreenCommon.h"
#include "ui/screens/SettingsScreen.h"

namespace screens {
    Action settings(ui::Context& ui, Screen& screen) {
        if (const Action action = detail::navigation(ui, Screen::Settings, screen); action != Action::None)
            return action;
        const ui::Rect content = detail::tabContent(ui);
        const uint8_t columns = content.w >= 280 ? 2 : 1;
        const int16_t rowHeight = columns == 2 ? 36 : 40;
        const int16_t gap = columns == 2 ? 6 : 4;
        auto reading = settingsPage::entries | std::views::filter([](const auto& entry) {
            return entry.section == UiText::ReadingSection;
        });
        auto systemEntries = settingsPage::entries | std::views::filter([](const auto& entry) {
            return entry.section == UiText::SystemSection;
        });
        ui.separator({content.x, content.y, content.w, 12}, ui.text(UiText::ReadingSection));
        ui::Grid grid{{content.x, static_cast<int16_t>(content.y + 18), content.w, content.h}, columns, rowHeight, gap};
        for (const auto& entry : reading) {
            const auto rect = grid.next(entry.span);
            if (ui.button(rect, ui.text(entry.label)))
                screen = entry.destination;
        }

        const int16_t readingRows = grid.rowsUsed();
        const int16_t systemY =
            static_cast<int16_t>(content.y + 24 + readingRows * rowHeight + (readingRows - 1) * gap);
        ui.separator({content.x, systemY, content.w, 12}, ui.text(UiText::SystemSection));
        ui::Grid system{{content.x, static_cast<int16_t>(systemY + 18), content.w, content.h}, columns, rowHeight, gap};
        for (const auto& entry : systemEntries)
            if (ui.button(system.next(entry.span), ui.text(entry.label)))
                screen = entry.destination;
        return Action::None;
    }
} // namespace screens
