#include "ui/screens/watch/Layout.h"
#include "ui/screens/SettingsScreen.h"

namespace screens {
    Action settings(ui::Context& ui, Screen& screen) {
        detail::navigation(ui, Screen::Settings, screen);
        const auto area = detail::tabContent(ui);
        if (ui.height() < 240) {
            const auto grid =
                ui.pagedGrid({area.x, static_cast<int16_t>(area.y + 18), area.w, static_cast<int16_t>(area.h - 18)},
                             settingsPage::entries.size(), 2, 48);
            ui.label({area.x, area.y, area.w, 18}, ui.text(settingsPage::entries[grid.first].section), 2, ui::themes::Muted);
            for (size_t index = grid.first; index < grid.first + grid.count; ++index) {
                const auto& entry = settingsPage::entries[index];
                if (ui.card(grid.item(index), ui.text(entry.label), {}, 2))
                    screen = entry.destination;
            }
            return Action::None;
        }
        auto reading = settingsPage::entries | std::views::filter([](const auto& entry) {
            return entry.section == UiText::ReadingSection;
        });
        auto systemEntries = settingsPage::entries | std::views::filter([](const auto& entry) {
            return entry.section == UiText::SystemSection;
        });
        constexpr int16_t heading = 24;
        const int16_t readingRows = ui::gridRows(reading, 2, &settingsPage::Entry::span);
        const int16_t systemRows = ui::gridRows(systemEntries, 2, &settingsPage::Entry::span);
        const int16_t row = (area.h - 2 * heading - 8) / (readingRows + systemRows);
        ui.label({area.x, area.y, area.w, heading}, ui.text(UiText::ReadingSection), 2, ui::themes::Muted);
        ui::Grid grid{{area.x, static_cast<int16_t>(area.y + heading), area.w, static_cast<int16_t>(row * readingRows + 4 * (readingRows - 1))},
                      2, row, 4};
        for (const auto& entry : reading) {
            const auto rect = grid.next(entry.span);
            if (ui.card(rect, ui.text(entry.label), {}, watch::textSize(ui)))
                screen = entry.destination;
        }
        const int16_t y = area.y + heading + row * readingRows + 4 * (readingRows - 1) + 4;
        ui.label({area.x, y, area.w, heading}, ui.text(UiText::SystemSection), 2, ui::themes::Muted);
        ui::Grid system{{area.x, static_cast<int16_t>(y + heading), area.w, row}, 2, row, 4};
        for (const auto& entry : systemEntries)
            if (ui.card(system.next(entry.span), ui.text(entry.label), {}, watch::textSize(ui)))
                screen = entry.destination;
        return Action::None;
    }
} // namespace screens
