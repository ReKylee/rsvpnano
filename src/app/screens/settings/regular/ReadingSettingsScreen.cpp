#include "app/screens/settings/SettingsScreens.h"
#include "app/screens/ScreenCommon.h"

#include "app/screens/settings/ReadingChoices.h"

namespace screens {
    bool readingSettings(ui::Context& ui, settings::ReadingSettings& config, Screen& screen) {
        bool changed = false;
        const ui::Rect content = detail::content(ui);
        constexpr int16_t gap = 6;
        constexpr int16_t backWidth = 56;
        constexpr int16_t topHeight = 44;
        if (ui.button({content.x, content.y, backWidth, topHeight}, "<<"))
            screen = Screen::Settings;
        changed |= ui.slider({static_cast<int16_t>(content.x + backWidth + gap), content.y,
                              static_cast<int16_t>(content.w - backWidth - gap), topHeight},
                             ui.text(UiText::WordsPerMinute), config.wpm, " WPM");

        const int16_t sectionY = static_cast<int16_t>(content.y + topHeight + gap);
        ui.separator({content.x, sectionY, content.w, 10},
                     ui.text(UiText::BehaviorSection));

        const int16_t halfWidth = static_cast<int16_t>((content.w - gap) / 2);
        const int16_t rowY = static_cast<int16_t>(sectionY + 14);
        constexpr int16_t rowHeight = 40;
        const int16_t toggleY = static_cast<int16_t>(rowY + rowHeight + gap);
        const int16_t height = content.y + content.h - toggleY;
        const std::array<ui::Rect, 4> controls{{
            {content.x, rowY, halfWidth, rowHeight},
            {static_cast<int16_t>(content.x + halfWidth + gap), rowY, halfWidth, rowHeight},
            {content.x, toggleY, halfWidth, height},
            {static_cast<int16_t>(content.x + halfWidth + gap), toggleY, halfWidth, height},
        }};
        changed |= editReadingChoices(config, [&](size_t index, UiText label, UiText value) {
            return ui.setting(controls[index], ui.text(label), ui.text(value), ui::SettingLayout::Inline);
        });
        return changed;
    }

} // namespace screens
