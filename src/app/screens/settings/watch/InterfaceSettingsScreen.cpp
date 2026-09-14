#include "app/screens/settings/InterfaceChoices.h"
#include "app/screens/settings/InterfaceScreen.h"
#include "app/screens/watch/Layout.h"

namespace screens {
    bool InterfaceScreen::draw(ui::Context& ui, settings::InterfaceSettings& config,
                               std::span<const uint32_t> standbyDurations, void (*setBrightness)(uint8_t),
                               Screen& screen) {
        auto area = watch::header(ui, ui.text(UiText::Display), Screen::Settings, screen);
        auto grid = ui.pagedGrid(area, 5, 1, 56);
        bool changed = false;
        if (watch::stepper(ui, grid.item(0), UiText::Brightness, config.brightnessPercent, "%")) {
            if (setBrightness)
                setBrightness(config.brightnessPercent);
            changed = true;
        }
        if (watch::setting(ui, grid.item(1), UiText::Theme, themes.resolve(config.selectedThemeId).definition.name)) {
            nextTheme(ui, config);
            changed = true;
        }
        if (watch::setting(ui, grid.item(2), UiText::Language,
                           languages_ ? locales::localeName(*languages_, config.locale) : config.locale)) {
            nextLocale(ui, config);
            changed = true;
        }
        char durationBuffer[16];
        const auto duration = standbyLabel(ui, config.standbyTimerIndex, standbyDurations, durationBuffer);
        if (watch::setting(ui, grid.item(3), UiText::Standby, duration)) {
            config.standbyTimerIndex.cycle();
            changed = true;
        }
        if (watch::setting(ui, grid.item(4), UiText::Screensaver, ui.text(screensaverLabel(config.screensaver)))) {
            config.screensaver = settings::cycleEnum(config.screensaver);
            changed = true;
        }
        return changed;
    }
} // namespace screens
