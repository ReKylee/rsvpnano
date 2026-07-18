#include "ui/screens/ScreenCommon.h"

namespace screens {
    bool pacingSettings(ui::Context& ui, ReadingLoop& reader, settings::PacingSettings& config, Screen& screen) {
        bool changed = false;
        const ui::Rect content = detail::content(ui);
        if (ui.button({content.x, content.y, 64, 24}, ui.text(UiText::Back)))
            screen = Screen::Settings;
        ui.label({static_cast<int16_t>(content.x + 74), content.y, static_cast<int16_t>(content.w - 74), 24},
                 ui.text(UiText::WordPacing), 2);

        const int16_t gap = 6;
        const int16_t cardWidth = static_cast<int16_t>((content.w - gap * 2) / 3);
        ui.separator({content.x, static_cast<int16_t>(content.y + 30), content.w, 10},
                     ui.text(UiText::AdditionalDelaySection));
        const int16_t sliderY = static_cast<int16_t>(content.y + 44);
        if (const auto value = ui.slider({content.x, sliderY, cardWidth, 50}, ui.text(UiText::LongWords),
                                         reader.pacingConfig().longWordDelayMs,
                                         decltype(config.longWordDelayMs)::min(),
                                         decltype(config.longWordDelayMs)::max(),
                                         decltype(config.longWordDelayMs)::step(), " ms");
            value.changed) {
            auto pacing = reader.pacingConfig();
            pacing.longWordDelayMs = static_cast<uint16_t>(value.value);
            config.longWordDelayMs = pacing.longWordDelayMs;
            reader.setPacingConfig(pacing);
            changed = true;
        }

        if (const auto value =
                ui.slider({static_cast<int16_t>(content.x + cardWidth + gap), sliderY, cardWidth, 50},
                          ui.text(UiText::Complexity),
                          reader.pacingConfig().complexWordDelayMs, decltype(config.complexWordDelayMs)::min(),
                          decltype(config.complexWordDelayMs)::max(), decltype(config.complexWordDelayMs)::step(),
                          " ms");
            value.changed) {
            auto pacing = reader.pacingConfig();
            pacing.complexWordDelayMs = static_cast<uint16_t>(value.value);
            config.complexWordDelayMs = pacing.complexWordDelayMs;
            reader.setPacingConfig(pacing);
            changed = true;
        }

        if (const auto value =
                ui.slider({static_cast<int16_t>(content.x + (cardWidth + gap) * 2), sliderY, cardWidth, 50},
                          ui.text(UiText::Punctuation), reader.pacingConfig().punctuationDelayMs,
                          decltype(config.punctuationDelayMs)::min(), decltype(config.punctuationDelayMs)::max(),
                          decltype(config.punctuationDelayMs)::step(), " ms");
            value.changed) {
            auto pacing = reader.pacingConfig();
            pacing.punctuationDelayMs = static_cast<uint16_t>(value.value);
            config.punctuationDelayMs = pacing.punctuationDelayMs;
            reader.setPacingConfig(pacing);
            changed = true;
        }

        if (ui.button({content.x, static_cast<int16_t>(sliderY + 58), content.w, 38},
                      ui.text(UiText::ResetPacing))) {
            config = {};
            const ReadingLoop::PacingConfig pacing{config.longWordDelayMs, config.complexWordDelayMs,
                                                   config.punctuationDelayMs};
            reader.setPacingConfig(pacing);
            changed = true;
        }
        return changed;
    }

} // namespace screens
