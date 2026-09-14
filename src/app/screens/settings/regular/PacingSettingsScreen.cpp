#include "app/screens/settings/PacingChoices.h"
#include "app/screens/settings/SettingsScreens.h"
#include "app/screens/ScreenCommon.h"

namespace screens {
    bool pacingSettings(ui::Context& ui, settings::PacingSettings& config, Screen& screen) {
        bool changed = false;
        const ui::Rect content = detail::content(ui);
        constexpr int16_t gap = 6;
        constexpr int16_t backWidth = 56;
        constexpr int16_t resetWidth = 90;
        const int16_t rowHeight = static_cast<int16_t>((content.h - gap * 2) / 3);

        if (ui.button({content.x, content.y, backWidth, rowHeight}, "<<"))
            screen = Screen::Settings;
        changed |= ui.slider({static_cast<int16_t>(content.x + backWidth + gap), content.y,
                              static_cast<int16_t>(content.w - backWidth - gap), rowHeight},
                             ui.text(kPacingChoices[0].label), config.*kPacingChoices[0].member, " ms");

        const int16_t secondRowY = static_cast<int16_t>(content.y + rowHeight + gap);
        changed |= ui.slider({content.x, secondRowY, content.w, rowHeight}, ui.text(kPacingChoices[1].label),
                             config.*kPacingChoices[1].member, " ms");

        const int16_t thirdRowY = static_cast<int16_t>(secondRowY + rowHeight + gap);
        if (ui.button({content.x, thirdRowY, resetWidth, rowHeight}, ui.text(UiText::Reset))) {
            changed |= config != settings::PacingSettings{};
            config = {};
        }
        changed |= ui.slider({static_cast<int16_t>(content.x + resetWidth + gap), thirdRowY,
                              static_cast<int16_t>(content.w - resetWidth - gap), rowHeight},
                             ui.text(kPacingChoices[2].label), config.*kPacingChoices[2].member, " ms");
        return changed;
    }

} // namespace screens
