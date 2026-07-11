#include "ui/screens/ScreenCommon.h"

#include "settings/PreferenceSpecs.h"

namespace screens {
    namespace pref = settings::prefs;

    void InterfaceScreen::begin(ui::Context& ui, Preferences& preferences, size_t standbyDurationCount,
                              void (*setBrightness)(uint8_t)) {
        config.brightnessIndex = settings::load<pref::BrightnessIndex>(preferences, 20U);
        config.standbyIndex = settings::load<pref::StandbyTimerIndex>(preferences, standbyDurationCount);
        config.language = static_cast<UiLanguage>(settings::load<pref::UiLanguage>(preferences));
        config.screensaver = static_cast<standby::Kind>(std::min<uint8_t>(
            settings::load<pref::ScreensaverMode>(preferences), static_cast<uint8_t>(standby::Kind::ScreenOff)));
        if (setBrightness != nullptr)
            setBrightness(static_cast<uint8_t>((config.brightnessIndex + 1U) * 5U));

        themes.loadFromSd();
        const std::string savedThemeId = settings::load<pref::ThemeId>(preferences);
        if (!savedThemeId.empty())
            themes.selectById(savedThemeId);
        ui.setTheme(themes.selected());
        ui.setLanguage(config.language);
    }

    void InterfaceScreen::draw(ui::Context& ui, Preferences& preferences, std::span<const uint32_t> standbyDurations,
                             void (*setBrightness)(uint8_t), Screen& screen) {
        const ui::Rect content = detail::content(ui);
        if (ui.button({content.x, content.y, 64, 24}, ui.text(UiText::Back)))
            screen = Screen::Settings;
        ui.label({static_cast<int16_t>(content.x + 74), content.y, static_cast<int16_t>(content.w - 74), 24},
                 "Interface", 2);

        const int brightnessPercent = (static_cast<int>(config.brightnessIndex) + 1) * 5;
        const int16_t controlsY = static_cast<int16_t>(content.y + 32);
        if (const auto brightness =
                ui.slider({content.x, controlsY, content.w, 40}, ui.text(UiText::Brightness), brightnessPercent, 5,
                          100, 5, "%");
            brightness.changed) {
            config.brightnessIndex = static_cast<uint8_t>(brightness.value / 5 - 1);
            settings::save<pref::BrightnessIndex>(preferences, config.brightnessIndex, 20U);
            if (setBrightness != nullptr)
                setBrightness(static_cast<uint8_t>(brightness.value));
        }

        const int16_t gap = 6;
        const int16_t halfWidth = static_cast<int16_t>((content.w - gap) / 2);
        const int16_t rowY = static_cast<int16_t>(controlsY + 47);
        if (ui.setting({content.x, rowY, halfWidth, 34}, ui.text(UiText::Theme), themes.selected().name)) {
            themes.selectNext();
            settings::save<pref::ThemeId>(preferences, themes.selected().id);
            ui.setTheme(themes.selected());
        }

        std::string standby = "Off";
        if (config.standbyIndex < standbyDurations.size() && standbyDurations[config.standbyIndex] != 0) {
            standby = std::to_string(standbyDurations[config.standbyIndex] / 60000UL) + "m";
        }
        if (ui.setting({static_cast<int16_t>(content.x + halfWidth + gap), rowY, halfWidth, 34}, "Standby", standby)) {
            config.standbyIndex = settings::cycle<pref::StandbyTimerIndex>(preferences, standbyDurations.size());
        }

        const int16_t secondRowY = static_cast<int16_t>(rowY + 40);
        if (ui.setting({content.x, secondRowY, halfWidth, 34}, ui.text(UiText::Language),
                       Localization::languageName(config.language))) {
            config.language = Localization::nextLanguage(config.language);
            settings::save<pref::UiLanguage>(preferences, static_cast<uint8_t>(config.language));
            ui.setLanguage(config.language);
        }

        const char* screensaver = config.screensaver == standby::Kind::Maze      ? "Maze"
                                : config.screensaver == standby::Kind::Voronoi   ? "Voronoi"
                                : config.screensaver == standby::Kind::ScreenOff ? "Screen off"
                                                                                : "Life";
        if (ui.setting({static_cast<int16_t>(content.x + halfWidth + gap), secondRowY, halfWidth, 34}, "Screensaver",
                       screensaver)) {
            config.screensaver = static_cast<standby::Kind>((static_cast<uint8_t>(config.screensaver) + 1U) % 4U);
            settings::save<pref::ScreensaverMode>(preferences, static_cast<uint8_t>(config.screensaver));
        }
    }

} // namespace screens
