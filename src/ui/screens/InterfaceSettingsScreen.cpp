#include "ui/screens/ScreenCommon.h"

#include "localization/LocaleCatalog.h"
#include "ui/screens/InterfaceSettingsLayout.h"
#include "ui/screens/SettingsChoices.h"

namespace screens {
    namespace {
        std::string_view nextLocale(const locales::Catalog& catalog, std::string_view current) {
            bool returnNext = current == Localization::kDefaultLocale;
            for (const auto& pack: catalog) {
                if (returnNext)
                    return pack.locale;
                returnNext = pack.locale == current;
            }
            return Localization::kDefaultLocale;
        }
    } // namespace

    bool InterfaceScreen::begin(ui::Context& ui, settings::InterfaceSettings& config, const locales::Catalog& languages,
                                void (*setBrightness)(uint8_t)) {
        languages_ = &languages;
        if (setBrightness != nullptr)
            setBrightness(config.brightnessPercent);

        themes.loadFromSd();
        const ui::themes::Theme& selected = themes.resolve(config.selectedThemeId);
        const bool corrected = config.selectedThemeId != selected.id;
        config.selectedThemeId = selected.id;
        ui.setTheme(selected);
        ui.setLocale(config.locale);
        return corrected;
    }

    bool InterfaceScreen::draw(ui::Context& ui, settings::InterfaceSettings& config,
                               std::span<const uint32_t> standbyDurations, void (*setBrightness)(uint8_t),
                               Screen& screen) {
        using interfaceLayout::Field;
        const auto layout = interfaceLayout::make(ui, screen);
        ui::SettingsControls controls{ui, layout.style};
        bool changed = false;

        if (controls.number(layout[Field::Brightness], UiText::Brightness, config.brightnessPercent, "%")
            && setBrightness != nullptr)
            setBrightness(config.brightnessPercent);

        if (controls.setting(layout[Field::Theme], UiText::Theme, [&]() -> std::string_view {
                return themes.resolve(config.selectedThemeId).definition.name;
            })) {
            const auto& next = themes.next(config.selectedThemeId);
            if (config.selectedThemeId != next.id) {
                config.selectedThemeId = next.id;
                ui.setTheme(next);
                changed = true;
            }
        }

        if (controls.setting(layout[Field::Language], UiText::Language, [&]() -> std::string_view {
                return languages_ ? locales::localeName(*languages_, config.locale) : std::string_view{config.locale};
            })) {
            const auto next = languages_ ? nextLocale(*languages_, config.locale) : Localization::kDefaultLocale;
            if (config.locale != next) {
                config.locale = next;
                ui.setLocale(config.locale);
                changed = true;
            }
        }

        if (controls.setting(layout[Field::Standby], UiText::Standby, [&] {
                const size_t index = config.standbyTimerIndex;
                return index < standbyDurations.size() && standbyDurations[index] != 0
                           ? std::to_string(standbyDurations[index] / 60000) + "m"
                           : std::string{ui.text(UiText::Off)};
            })) {
            config.standbyTimerIndex.cycle();
            changed = true;
        }
        controls.choice(layout[Field::Screensaver], UiText::Screensaver, config.screensaver, choices::screensaver);
        return changed || controls.changed();
    }

} // namespace screens
