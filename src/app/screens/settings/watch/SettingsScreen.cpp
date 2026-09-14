#include "app/screens/settings/SettingsMenu.h"
#include "app/screens/settings/SettingsScreens.h"
#include "app/screens/watch/Layout.h"

namespace screens {
    Action settings(ui::Context& ui, Screen& screen) {
        detail::navigation(ui, Screen::Settings, screen);
        const auto area = detail::tabContent(ui);
        if (ui.height() < 240) {
            const auto grid =
                ui.pagedGrid({area.x, static_cast<int16_t>(area.y + 18), area.w, static_cast<int16_t>(area.h - 18)}, kSettingsMenu.size(),
                             2, 48);
            ui.label({area.x, area.y, area.w, 18},
                     ui.text(grid.first < kReadingMenu.size() ? UiText::ReadingSection : UiText::SystemSection), 2, ui::themes::Muted);
            for (size_t i = grid.first; i < grid.first + grid.count; ++i)
                if (ui.card(grid.item(i), ui.text(kSettingsMenu[i].label), {}, 2))
                    screen = kSettingsMenu[i].target;
            return Action::None;
        }
        constexpr int16_t heading = 24;
        const int16_t row = (area.h - 2 * heading - 8) / 3;
        ui.label({area.x, area.y, area.w, heading}, ui.text(UiText::ReadingSection), 2, ui::themes::Muted);
        ui::Grid grid{{area.x, static_cast<int16_t>(area.y + heading), area.w, static_cast<int16_t>(row * 2 + 4)},
                      2,
                      row,
                      4};
        for (const auto& entry: kReadingMenu) {
            auto item = grid.next();
            if (entry.fullRow)
                item.w = area.w;
            if (ui.card(item, ui.text(entry.label), {}, watch::textSize(ui)))
                screen = entry.target;
        }
        const int16_t y = area.y + heading + row * 2 + 8;
        ui.label({area.x, y, area.w, heading}, ui.text(UiText::SystemSection), 2, ui::themes::Muted);
        ui::Grid system{{area.x, static_cast<int16_t>(y + heading), area.w, row}, 2, row, 4};
        for (const auto& entry: kSystemMenu)
            if (ui.card(system.next(), ui.text(entry.label), {}, watch::textSize(ui)))
                screen = entry.target;
        return Action::None;
    }
} // namespace screens
