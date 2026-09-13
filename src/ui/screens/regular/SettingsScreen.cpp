#include "ui/screens/ScreenCommon.h"
#include "ui/screens/SettingsMenu.h"

namespace screens {
    Action settings(ui::Context& ui, Screen& screen) {
        if (const Action action = detail::navigation(ui, Screen::Settings, screen); action != Action::None)
            return action;
        const ui::Rect content = detail::tabContent(ui);
        const uint8_t columns = content.w >= 280 ? 2 : 1;
        const int16_t rowHeight = columns == 2 ? 36 : 40;
        const int16_t gap = columns == 2 ? 6 : 4;
        ui.separator({content.x, content.y, content.w, 12}, ui.text(UiText::ReadingSection));
        ui::Grid grid{{content.x, static_cast<int16_t>(content.y + 18), content.w, content.h}, columns, rowHeight, gap};
        for (const auto& entry : readingEntries) {
            const auto rect = settingsItem(grid, entry);
            if (ui.button(rect, ui.text(entry.label)))
                screen = entry.destination;
        }

        const int16_t readingRows = settingsRows(readingEntries, columns);
        const int16_t systemY =
            static_cast<int16_t>(content.y + 24 + readingRows * rowHeight + (readingRows - 1) * gap);
        ui.separator({content.x, systemY, content.w, 12}, ui.text(UiText::SystemSection));
        ui::Grid system{{content.x, static_cast<int16_t>(systemY + 18), content.w, content.h}, columns, rowHeight, gap};
        for (const auto& entry : systemEntries)
            if (ui.button(settingsItem(system, entry), ui.text(entry.label)))
                screen = entry.destination;
        return Action::None;
    }
} // namespace screens
