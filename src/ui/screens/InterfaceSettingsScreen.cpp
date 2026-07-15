#include "ui/screens/ScreenCommon.h"

#include <algorithm>

#include "settings/PreferenceSpecs.h"

namespace screens {
    namespace pref = settings::prefs;

    void InterfaceScreen::begin(ui::Context& ui, Preferences& preferences, const FontCatalog& fonts,
                                void (*setBrightness)(uint8_t)) {
        config.brightnessIndex = settings::load<pref::BrightnessIndex>(preferences);
        config.standbyIndex = settings::load<pref::StandbyTimerIndex>(preferences);
        config.language = settings::load<pref::UiLanguage>(preferences);
        config.screensaver = settings::load<pref::ScreensaverMode>(preferences);
        if (setBrightness != nullptr)
            setBrightness(static_cast<uint8_t>((config.brightnessIndex + 1U) * 5U));

        themes.loadFromSd(fonts);
        const std::string savedThemeId = settings::load<pref::ThemeId>(preferences);
        if (!savedThemeId.empty())
            themes.selectById(savedThemeId);
        ui.setTheme(themes.selected());
        ui.setLanguage(config.language);
    }

    bool InterfaceScreen::draw(ui::Context& ui, Preferences& preferences,
                               std::span<const uint32_t> standbyDurations, void (*setBrightness)(uint8_t),
                               Screen& screen) {
        bool themeChanged = false;
        const ui::Rect content = detail::content(ui);
        if (ui.button({content.x, content.y, 64, 24}, ui.text(UiText::Back)))
            screen = Screen::Settings;
        ui.label({static_cast<int16_t>(content.x + 74), content.y, static_cast<int16_t>(content.w - 74), 24},
                 ui.text(UiText::Interface), 2);

        const int brightnessPercent = (static_cast<int>(config.brightnessIndex) + 1) * 5;
        const int16_t controlsY = static_cast<int16_t>(content.y + 32);
        const int16_t sliderWidth = std::min<int16_t>(content.w, 480);
        const int16_t sliderX = static_cast<int16_t>(content.x + (content.w - sliderWidth) / 2);
        if (const auto brightness =
                ui.slider({sliderX, controlsY, sliderWidth, 34}, ui.text(UiText::Brightness), brightnessPercent, 5,
                          100, 5, "%");
            brightness.changed) {
            config.brightnessIndex = static_cast<uint8_t>(brightness.value / 5 - 1);
            settings::save<pref::BrightnessIndex>(preferences, config.brightnessIndex);
            if (setBrightness != nullptr)
                setBrightness(static_cast<uint8_t>(brightness.value));
        }

        const int16_t gap = 6;
        const int16_t halfWidth = static_cast<int16_t>((content.w - gap) / 2);
        const int16_t sectionsY = static_cast<int16_t>(controlsY + 40);
        ui.separator({content.x, sectionsY, halfWidth, 10}, ui.text(UiText::AppearanceSection));
        ui.separator({static_cast<int16_t>(content.x + halfWidth + gap), sectionsY, halfWidth, 10},
                     ui.text(UiText::StandbySection));

        const int16_t firstRowY = static_cast<int16_t>(sectionsY + 14);
        const int16_t secondRowY = static_cast<int16_t>(firstRowY + 37);
        if (ui.setting({content.x, firstRowY, halfWidth, 32}, ui.text(UiText::Theme), themes.selected().name,
                       ui::SettingLayout::Inline)) {
            themes.selectNext();
            settings::save<pref::ThemeId>(preferences, themes.selected().id);
            ui.setTheme(themes.selected());
            themeChanged = true;
        }

        if (ui.setting({content.x, secondRowY, halfWidth, 32}, ui.text(UiText::Language),
                       Localization::languageName(config.language), ui::SettingLayout::Inline)) {
            config.language = Localization::nextLanguage(config.language);
            settings::save<pref::UiLanguage>(preferences, config.language);
            ui.setLanguage(config.language);
        }

        std::string standby{ui.text(UiText::Off)};
        if (config.standbyIndex < standbyDurations.size() && standbyDurations[config.standbyIndex] != 0) {
            standby = std::to_string(standbyDurations[config.standbyIndex] / 60000UL) + "m";
        }
        if (ui.setting({static_cast<int16_t>(content.x + halfWidth + gap), firstRowY, halfWidth, 32},
                       ui.text(UiText::Standby), standby, ui::SettingLayout::Inline)) {
            config.standbyIndex = settings::cycle<pref::StandbyTimerIndex>(preferences);
        }

        const UiText screensaver = config.screensaver == standby::Kind::Maze      ? UiText::Maze
                                  : config.screensaver == standby::Kind::Voronoi   ? UiText::Voronoi
                                  : config.screensaver == standby::Kind::Reaction  ? UiText::Reaction
                                  : config.screensaver == standby::Kind::ScreenOff ? UiText::ScreenOff
                                                                                  : UiText::Life;
        if (ui.setting({static_cast<int16_t>(content.x + halfWidth + gap), secondRowY, halfWidth, 32},
                       ui.text(UiText::Screensaver), ui.text(screensaver), ui::SettingLayout::Inline)) {
            config.screensaver = settings::cycle<pref::ScreensaverMode>(preferences);
        }
        return themeChanged;
    }

} // namespace screens
