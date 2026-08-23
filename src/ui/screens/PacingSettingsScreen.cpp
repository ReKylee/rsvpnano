#include "ui/screens/ScreenCommon.h"

namespace screens {
    bool pacingSettings(ui::Context& ui, settings::PacingSettings& config, Screen& screen) {
        bool changed = false;
        const ui::Rect content = detail::content(ui);
        if (ui.button({content.x, content.y, 64, detail::kBackButtonHeight}, "<<"))
            screen = Screen::Settings;
        ui.label({static_cast<int16_t>(content.x + 74), content.y, static_cast<int16_t>(content.w - 74), 24},
                 ui.text(UiText::WordPacing), 2);

        const int16_t gap = 6;
        const int16_t cardWidth = static_cast<int16_t>((content.w - gap * 2) / 3);
        ui.separator({content.x, static_cast<int16_t>(content.y + 30), content.w, 10},
                     ui.text(UiText::AdditionalDelaySection));
        const int16_t sliderY = static_cast<int16_t>(content.y + 44);
        changed |=
            ui.slider({content.x, sliderY, cardWidth, 50}, ui.text(UiText::LongWords), config.longWordDelayMs, " ms");
        changed |= ui.slider({static_cast<int16_t>(content.x + cardWidth + gap), sliderY, cardWidth, 50},
                             ui.text(UiText::Complexity), config.complexWordDelayMs, " ms");
        changed |= ui.slider({static_cast<int16_t>(content.x + (cardWidth + gap) * 2), sliderY, cardWidth, 50},
                             ui.text(UiText::Punctuation), config.punctuationDelayMs, " ms");

        if (ui.button({content.x, static_cast<int16_t>(sliderY + 58), content.w, 38}, ui.text(UiText::ResetPacing))) {
            config = {};
            changed = true;
        }
        return changed;
    }

} // namespace screens
