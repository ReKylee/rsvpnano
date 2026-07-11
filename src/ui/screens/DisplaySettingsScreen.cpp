#include "ui/screens/ScreenCommon.h"

#include "settings/PreferenceSpecs.h"

namespace screens {
    namespace pref = settings::prefs;

    void DisplayScreen::begin(ui::Context& ui, Preferences& preferences, size_t standbyDurationCount,
                              void (*setBrightness)(uint8_t)) {
        config.brightnessIndex = settings::load<pref::BrightnessIndex>(preferences, 20U);
        config.standbyIndex = settings::load<pref::StandbyTimerIndex>(preferences, standbyDurationCount);
        if (setBrightness != nullptr)
            setBrightness(static_cast<uint8_t>((config.brightnessIndex + 1U) * 5U));

        themes.loadFromSd();
        const String savedThemeId = settings::load<pref::ThemeId>(preferences);
        if (!savedThemeId.isEmpty())
            themes.selectById({savedThemeId.c_str(), savedThemeId.length()});
        ui.setTheme(themes.selected());
    }

    void DisplayScreen::draw(ui::Context& ui, Preferences& preferences, std::span<const uint32_t> standbyDurations,
                             void (*setBrightness)(uint8_t), Screen& screen) {
        ui::Column column{detail::content(ui), 5};
        if (ui.button(column.next(22), "Back"))
            screen = Screen::Settings;

        ui.label(column.next(16), "Brightness", 1, ui::themes::ColorRole::Muted);
        const int brightnessPercent = (static_cast<int>(config.brightnessIndex) + 1) * 5;
        if (const auto brightness = ui.slider(column.next(28), brightnessPercent, 5, 100, 5); brightness.changed) {
            config.brightnessIndex = static_cast<uint8_t>(brightness.value / 5 - 1);
            settings::save<pref::BrightnessIndex>(preferences, config.brightnessIndex, 20U);
            if (setBrightness != nullptr)
                setBrightness(static_cast<uint8_t>(brightness.value));
        }

        String theme = "Theme: ";
        theme += themes.selected().name.c_str();
        if (ui.button(column.next(28), theme.c_str())) {
            themes.selectNext();
            settings::save<pref::ThemeId>(preferences, String(themes.selected().id.c_str()));
            ui.setTheme(themes.selected());
        }

        String standby = "Standby: Off";
        if (config.standbyIndex < standbyDurations.size() && standbyDurations[config.standbyIndex] != 0) {
            standby = "Standby: " + String(standbyDurations[config.standbyIndex] / 60000UL) + "m";
        }
        if (ui.button(column.next(28), standby.c_str())) {
            config.standbyIndex = settings::cycle<pref::StandbyTimerIndex>(preferences, standbyDurations.size());
        }
    }

} // namespace screens
