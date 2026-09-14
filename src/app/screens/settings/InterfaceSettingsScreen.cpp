#include "app/screens/settings/InterfaceScreen.h"

#include "localization/LocaleCatalog.h"

namespace screens {
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

    void InterfaceScreen::nextTheme(ui::Context& ui, settings::InterfaceSettings& config) {
        const auto& selected = themes.next(config.selectedThemeId);
        config.selectedThemeId = selected.id;
        ui.setTheme(selected);
    }

    void InterfaceScreen::nextLocale(ui::Context& ui, settings::InterfaceSettings& config) {
        std::string_view next = Localization::kDefaultLocale;
        bool found = config.locale == Localization::kDefaultLocale;
        if (languages_)
            for (const auto& pack: *languages_) {
                if (found) {
                    next = pack.locale;
                    break;
                }
                found = pack.locale == config.locale;
            }
        config.locale = next;
        ui.setLocale(config.locale);
    }

} // namespace screens
