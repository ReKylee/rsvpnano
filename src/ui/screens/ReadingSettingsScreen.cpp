#include "ui/screens/ScreenCommon.h"

#include <algorithm>

#include "settings/PreferenceSpecs.h"

namespace screens {
    namespace pref = settings::prefs;

    void readingSettings(ui::Context& ui, ReadingLoop& reader, ReaderSettings& config, Preferences& preferences,
                         Screen& screen) {
        const ui::Rect content = detail::content(ui);
        if (ui.button({content.x, content.y, 64, 24}, ui.text(UiText::Back)))
            screen = Screen::Settings;
        ui.label({static_cast<int16_t>(content.x + 74), content.y, static_cast<int16_t>(content.w - 74), 24},
                 "Reading", 2);

        const int16_t controlsY = static_cast<int16_t>(content.y + 30);
        const int16_t sliderWidth = std::min<int16_t>(content.w, 480);
        const int16_t sliderX = static_cast<int16_t>(content.x + (content.w - sliderWidth) / 2);
        if (const auto wpm = ui.slider({sliderX, controlsY, sliderWidth, 34}, "Words per minute", reader.wpm(), 10,
                                       1000, 10, " WPM");
            wpm.changed) {
            reader.setWpm(static_cast<uint16_t>(wpm.value));
            settings::save<pref::Wpm>(preferences, reader.wpm());
        }

        ui.separator({content.x, static_cast<int16_t>(controlsY + 40), content.w, 10}, "BEHAVIOR");

        const int16_t gap = 6;
        const int16_t halfWidth = static_cast<int16_t>((content.w - gap) / 2);
        const int16_t rowY = static_cast<int16_t>(controlsY + 54);
        if (ui.setting({content.x, rowY, halfWidth, 42}, "Pause",
                       config.pauseMode == PauseMode::SentenceEnd ? "Sentence end" : "Instant")) {
            config.pauseMode = config.pauseMode == PauseMode::SentenceEnd ? PauseMode::Instant : PauseMode::SentenceEnd;
            settings::save<pref::PauseMode>(preferences, static_cast<uint8_t>(config.pauseMode));
        }

        if (ui.toggle({static_cast<int16_t>(content.x + halfWidth + gap), rowY, halfWidth, 42},
                      ui.text(UiText::PhantomWords),
                      config.phantomWords)) {
            config.phantomWords = settings::toggle<pref::PhantomWords>(preferences);
        }
    }

} // namespace screens
