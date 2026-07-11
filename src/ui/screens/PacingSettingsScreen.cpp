#include "ui/screens/ScreenCommon.h"

#include "settings/PreferenceSpecs.h"

namespace screens {
    namespace pref = settings::prefs;

    void pacingSettings(ui::Context& ui, ReadingLoop& reader, Preferences& preferences, Screen& screen) {
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
        if (const auto value = ui.slider({content.x, sliderY, cardWidth, 34}, ui.text(UiText::LongWords),
                                         reader.pacingConfig().longWordDelayMs, 0, 600, 50, " ms");
            value.changed) {
            auto pacing = reader.pacingConfig();
            pacing.longWordDelayMs = static_cast<uint16_t>(value.value);
            settings::save<pref::PacingLongWordDelay>(preferences, pacing.longWordDelayMs);
            reader.setPacingConfig(pacing);
        }

        if (const auto value =
                ui.slider({static_cast<int16_t>(content.x + cardWidth + gap), sliderY, cardWidth, 34},
                          ui.text(UiText::Complexity),
                          reader.pacingConfig().complexWordDelayMs, 0, 600, 50, " ms");
            value.changed) {
            auto pacing = reader.pacingConfig();
            pacing.complexWordDelayMs = static_cast<uint16_t>(value.value);
            settings::save<pref::PacingComplexWordDelay>(preferences, pacing.complexWordDelayMs);
            reader.setPacingConfig(pacing);
        }

        if (const auto value =
                ui.slider({static_cast<int16_t>(content.x + (cardWidth + gap) * 2), sliderY, cardWidth, 34},
                          ui.text(UiText::Punctuation), reader.pacingConfig().punctuationDelayMs, 0, 600, 50, " ms");
            value.changed) {
            auto pacing = reader.pacingConfig();
            pacing.punctuationDelayMs = static_cast<uint16_t>(value.value);
            settings::save<pref::PacingPunctuationDelay>(preferences, pacing.punctuationDelayMs);
            reader.setPacingConfig(pacing);
        }

        if (ui.button({content.x, static_cast<int16_t>(sliderY + 42), content.w, 34},
                      ui.text(UiText::ResetPacing))) {
            const ReadingLoop::PacingConfig pacing{pref::PacingLongWordDelay::defaultValue(),
                                                   pref::PacingComplexWordDelay::defaultValue(),
                                                   pref::PacingPunctuationDelay::defaultValue()};
            reader.setPacingConfig(pacing);
            settings::save<pref::PacingLongWordDelay>(preferences, pacing.longWordDelayMs);
            settings::save<pref::PacingComplexWordDelay>(preferences, pacing.complexWordDelayMs);
            settings::save<pref::PacingPunctuationDelay>(preferences, pacing.punctuationDelayMs);
        }
    }

} // namespace screens
