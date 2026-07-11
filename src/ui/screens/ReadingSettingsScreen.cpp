#include "ui/screens/ScreenCommon.h"

#include "settings/PreferenceSpecs.h"

namespace screens {
    namespace pref = settings::prefs;

    void readingSettings(ui::Context& ui, ReadingLoop& reader, ReaderSettings& config, Preferences& preferences,
                         Screen& screen) {
        ui::Column column{detail::content(ui), 2};
        if (ui.button(column.next(22), "Back"))
            screen = Screen::Settings;

        ui.label(column.next(16), "Words per minute", 1, ui::themes::ColorRole::Muted);
        if (const auto wpm = ui.slider(column.next(26), reader.wpm(), 100, 1500, 25); wpm.changed) {
            reader.setWpm(static_cast<uint16_t>(wpm.value));
            settings::save<pref::Wpm>(preferences, reader.wpm());
        }

        const std::string pause =
            std::string{"Pause: "} + (config.pauseMode == PauseMode::SentenceEnd ? "Sentence end" : "Instant");
        if (ui.button(column.next(24), pause)) {
            config.pauseMode = config.pauseMode == PauseMode::SentenceEnd ? PauseMode::Instant : PauseMode::SentenceEnd;
            settings::save<pref::PauseMode>(preferences, static_cast<uint8_t>(config.pauseMode));
        }

        const std::string phantom = std::string{"Phantom words: "} + (config.phantomWords ? "On" : "Off");
        if (ui.button(column.next(24), phantom)) {
            config.phantomWords = settings::toggle<pref::PhantomWords>(preferences);
        }

        const std::string chapterScroll =
            std::string{"Chapter scroll: "} + (config.chapterScrollReversed ? "Reversed" : "Normal");
        if (ui.button(column.next(24), chapterScroll))
            config.chapterScrollReversed = settings::toggle<pref::ChapterScrollReversed>(preferences);
    }

} // namespace screens
