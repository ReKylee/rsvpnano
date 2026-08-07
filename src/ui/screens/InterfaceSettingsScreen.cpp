#include "ui/screens/ScreenCommon.h"

#include <algorithm>

#include "locales/LocaleCatalog.h"

namespace screens {
    namespace {

        std::string_view nextLocale(const locales::Catalog& catalog, std::string_view current) {
            bool returnNext = current == Localization::kDefaultLocale;
            for (const auto& pack: catalog.packs) {
                if (returnNext)
                    return pack.manifest.locale;
                returnNext = pack.manifest.locale == current;
            }
            return Localization::kDefaultLocale;
        }

    } // namespace

    void InterfaceScreen::begin(ui::Context& ui, settings::InterfaceSettings& config,
                                const locales::Catalog& languages, void (*setBrightness)(uint8_t)) {
        languages_ = std::cref(languages);
        if (setBrightness != nullptr)
            setBrightness(config.brightnessPercent);

        themes.loadFromSd();
        if (!themes.selectById(config.selectedThemeId)) {
            config.selectedThemeId = themes.selected().id;
        }
        ui.setTheme(themes.selected());
        ui.setLocale(config.locale);
    }

    bool InterfaceScreen::draw(ui::Context& ui, settings::InterfaceSettings& config,
                               std::span<const uint32_t> standbyDurations, void (*setBrightness)(uint8_t),
                               Screen& screen) {
        bool changed = false;
        const ui::Rect content = detail::content(ui);
        if (ui.button({content.x, content.y, 64, detail::kBackButtonHeight}, "<<"))
            screen = Screen::Settings;
        ui.label({static_cast<int16_t>(content.x + 74), content.y, static_cast<int16_t>(content.w - 74), 24},
                 ui.text(UiText::Interface), 2);

        const int16_t controlsY = static_cast<int16_t>(content.y + detail::kBackButtonHeight);
        const int16_t sliderWidth = std::min<int16_t>(content.w, 480);
        const int16_t sliderX = static_cast<int16_t>(content.x + (content.w - sliderWidth) / 2);
        if (ui.slider({sliderX, controlsY, sliderWidth, 34}, ui.text(UiText::Brightness), config.brightnessPercent,
                      "%")) {
            if (setBrightness != nullptr)
                setBrightness(config.brightnessPercent);
            changed = true;
        }

        const int16_t gap = 6;
        const int16_t halfWidth = static_cast<int16_t>((content.w - gap) / 2);
        const int16_t sectionsY = static_cast<int16_t>(controlsY + 36);
        ui.separator({content.x, sectionsY, halfWidth, 10}, ui.text(UiText::AppearanceSection));
        ui.separator({static_cast<int16_t>(content.x + halfWidth + gap), sectionsY, halfWidth, 10},
                     ui.text(UiText::StandbySection));

        const int16_t firstRowY = static_cast<int16_t>(sectionsY + 14);
        const int16_t secondRowY = static_cast<int16_t>(firstRowY + 36);
        if (ui.setting({content.x, firstRowY, halfWidth, 34}, ui.text(UiText::Theme), themes.selected().definition.name,
                       ui::SettingLayout::Inline)) {
            themes.selectNext();
            config.selectedThemeId = themes.selected().id;
            ui.setTheme(themes.selected());
            changed = true;
        }

        if (ui.setting({content.x, secondRowY, halfWidth, 34}, ui.text(UiText::Language),
                       languages_ ? locales::localeName(languages_->get(), config.locale)
                                  : std::string_view{config.locale},
                       ui::SettingLayout::Inline)) {
            config.locale = !languages_ ? std::string{Localization::kDefaultLocale}
                                        : std::string{nextLocale(languages_->get(), config.locale)};
            ui.setLocale(config.locale);
            changed = true;
        }

        std::string standby{ui.text(UiText::Off)};
        const size_t standbyIndex = config.standbyTimerIndex;
        if (standbyIndex < standbyDurations.size() && standbyDurations[standbyIndex] != 0) {
            standby = std::to_string(standbyDurations[standbyIndex] / 60000UL) + "m";
        }
        if (ui.setting({static_cast<int16_t>(content.x + halfWidth + gap), firstRowY, halfWidth, 34},
                       ui.text(UiText::Standby), standby, ui::SettingLayout::Inline)) {
            config.standbyTimerIndex.cycle();
            changed = true;
        }

        const UiText screensaver = config.screensaver == standby::Kind::maze      ? UiText::Maze
                                 : config.screensaver == standby::Kind::voronoi   ? UiText::Voronoi
                                 : config.screensaver == standby::Kind::reaction  ? UiText::Reaction
                                 : config.screensaver == standby::Kind::screenOff ? UiText::ScreenOff
                                                                                  : UiText::Life;
        if (ui.setting({static_cast<int16_t>(content.x + halfWidth + gap), secondRowY, halfWidth, 34},
                       ui.text(UiText::Screensaver), ui.text(screensaver), ui::SettingLayout::Inline)) {
            config.screensaver = settings::cycleEnum(config.screensaver);
            changed = true;
        }
        return changed;
    }

} // namespace screens
