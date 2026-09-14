#include "app/screens/settings/ReadingChoices.h"
#include "app/screens/settings/SettingsScreens.h"
#include "app/screens/watch/Layout.h"

namespace screens {
    bool readingSettings(ui::Context& ui, settings::ReadingSettings& config, Screen& screen) {
        auto area = watch::header(ui, ui.text(UiText::Reading), Screen::Settings, screen);
        const auto grid = ui.pagedGrid(area, 5, 1, 72);
        bool changed = false;
        const auto speed = grid.item(0);
        if (speed.w > 0) {
            const int16_t size = std::min<int16_t>(speed.h, speed.w / 2);
            ui::Rect dial{static_cast<int16_t>(speed.x + (speed.w - size) / 2), speed.y, size, speed.h};
            int value = config.wpm;
            changed |= ui.rotary(dial, value, config.wpm.min(), config.wpm.max(), config.wpm.step(), "WPM");
            if (ui.button({speed.x, speed.y, 48, speed.h}, "-")) {
                value = std::max<int>(config.wpm.min(), value - config.wpm.step());
                changed = true;
            }
            if (ui.button({static_cast<int16_t>(speed.x + speed.w - 48), speed.y, 48, speed.h}, "+")) {
                value = std::min<int>(config.wpm.max(), value + config.wpm.step());
                changed = true;
            }
            config.wpm = value;
        }
        changed |= editReadingChoices(config, [&](size_t index, UiText label, UiText value) {
            return watch::setting(ui, grid.item(index + 1), label, ui.text(value));
        });
        return changed;
    }
} // namespace screens
