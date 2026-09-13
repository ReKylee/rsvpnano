#include "ui/screens/ScreenCommon.h"

#include "localization/LocaleCatalog.h"
#include "ui/screens/InterfaceSettingsLayout.h"

namespace screens {
    namespace {
        constexpr std::array screensavers{standby::Kind::life, standby::Kind::maze, standby::Kind::voronoi,
                                         standby::Kind::screenOff, standby::Kind::reaction};
        static_assert(screensavers.size() == static_cast<size_t>(standby::Kind::Count));
        constexpr auto screensaverLabel = [](standby::Kind kind) {
            switch (kind) {
            case standby::Kind::life: return UiText::Life;
            case standby::Kind::maze: return UiText::Maze;
            case standby::Kind::voronoi: return UiText::Voronoi;
            case standby::Kind::screenOff: return UiText::ScreenOff;
            case standby::Kind::reaction: return UiText::Reaction;
            default: return UiText::Unknown;
            }
        };

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
        bool changed = ui::number(ui, layout[Field::Brightness], UiText::Brightness,
                                  config.brightnessPercent, "%", layout.number);
        if (changed && setBrightness != nullptr)
            setBrightness(config.brightnessPercent);

        if (ui::valueButton(ui, layout[Field::Theme], UiText::Theme, [&]() -> std::string_view {
                return themes.resolve(config.selectedThemeId).definition.name;
            }, layout.style)) {
            const auto& next = themes.next(config.selectedThemeId);
            if (config.selectedThemeId != next.id) {
                config.selectedThemeId = next.id;
                ui.setTheme(next);
                changed = true;
            }
        }

        if (ui::valueButton(ui, layout[Field::Language], UiText::Language, [&]() -> std::string_view {
                return languages_ ? locales::localeName(*languages_, config.locale) : std::string_view{config.locale};
            }, layout.style)) {
            const auto next = languages_ ? nextLocale(*languages_, config.locale) : Localization::kDefaultLocale;
            if (config.locale != next) {
                config.locale = next;
                ui.setLocale(config.locale);
                changed = true;
            }
        }

        if (ui::valueButton(ui, layout[Field::Standby], UiText::Standby, [&] {
                const size_t index = config.standbyTimerIndex;
                return index < standbyDurations.size() && standbyDurations[index] != 0
                           ? std::to_string(standbyDurations[index] / 60000) + "m"
                           : std::string{ui.text(UiText::Off)};
            }, layout.style)) {
            config.standbyTimerIndex.cycle();
            changed = true;
        }
        changed |= ui::select(ui, layout[Field::Screensaver], UiText::Screensaver, config.screensaver,
                              screensavers, screensaverLabel, layout.style);
        return changed;
    }

} // namespace screens
