#include "app/screens/settings/InterfaceChoices.h"
#include "app/screens/settings/InterfaceScreen.h"
#include "app/screens/ScreenCommon.h"

#include "localization/LocaleCatalog.h"

namespace screens {
    bool InterfaceScreen::draw(ui::Context& ui, settings::InterfaceSettings& config,
                               std::span<const uint32_t> standbyDurations, void (*setBrightness)(uint8_t),
                               Screen& screen) {
        bool changed = false;
        const ui::Rect content = detail::content(ui);
        constexpr int16_t gap = 6;
        constexpr int16_t backWidth = 56;
        constexpr int16_t topHeight = 42;
        if (ui.button({content.x, content.y, backWidth, topHeight}, "<<"))
            screen = Screen::Settings;
        if (ui.slider({static_cast<int16_t>(content.x + backWidth + gap), content.y,
                       static_cast<int16_t>(content.w - backWidth - gap), topHeight},
                      ui.text(UiText::Brightness), config.brightnessPercent, "%")) {
            if (setBrightness != nullptr)
                setBrightness(config.brightnessPercent);
            changed = true;
        }

        const int16_t halfWidth = static_cast<int16_t>((content.w - gap) / 2);
        const int16_t sectionsY = static_cast<int16_t>(content.y + topHeight + 4);
        ui.separator({content.x, sectionsY, halfWidth, 10}, ui.text(UiText::AppearanceSection));
        ui.separator({static_cast<int16_t>(content.x + halfWidth + gap), sectionsY, halfWidth, 10},
                     ui.text(UiText::StandbySection));

        const int16_t firstRowY = static_cast<int16_t>(sectionsY + 14);
        constexpr int16_t rowHeight = 40;
        const int16_t secondRowY = static_cast<int16_t>(firstRowY + rowHeight + 4);
        const ui::themes::Theme& selectedTheme = themes.resolve(config.selectedThemeId);
        if (ui.setting({content.x, firstRowY, halfWidth, rowHeight}, ui.text(UiText::Theme),
                       selectedTheme.definition.name, ui::SettingLayout::Inline)) {
            nextTheme(ui, config);
            changed = true;
        }

        if (ui.setting({content.x, secondRowY, halfWidth, rowHeight}, ui.text(UiText::Language),
                       languages_ ? locales::localeName(*languages_, config.locale) : std::string_view{config.locale},
                       ui::SettingLayout::Inline)) {
            nextLocale(ui, config);
            changed = true;
        }

        char durationBuffer[16];
        const auto standby = standbyLabel(ui, config.standbyTimerIndex, standbyDurations, durationBuffer);
        if (ui.setting({static_cast<int16_t>(content.x + halfWidth + gap), firstRowY, halfWidth, rowHeight},
                       ui.text(UiText::Standby), standby, ui::SettingLayout::Inline)) {
            config.standbyTimerIndex.cycle();
            changed = true;
        }

        if (ui.setting({static_cast<int16_t>(content.x + halfWidth + gap), secondRowY, halfWidth, rowHeight},
                       ui.text(UiText::Screensaver), ui.text(screensaverLabel(config.screensaver)), ui::SettingLayout::Inline)) {
            config.screensaver = settings::cycleEnum(config.screensaver);
            changed = true;
        }
        return changed;
    }

} // namespace screens
