#include "ui/screens/ScreenCommon.h"

#include "settings/PreferenceSpecs.h"

namespace screens {
    namespace pref = settings::prefs;

    void pacingSettings(ui::Context& ui, ReadingLoop& reader, Preferences& preferences, Screen& screen) {
        ui::Column column{detail::content(ui), 3};
        if (ui.button(column.next(22), "Back"))
            screen = Screen::Settings;

        ui.label(column.next(12), "Long words", 1, ui::themes::ColorRole::Muted);
        if (const auto value = ui.slider(column.next(22), reader.pacingConfig().longWordDelayMs, 0, 600, 50);
            value.changed) {
            auto pacing = reader.pacingConfig();
            pacing.longWordDelayMs = static_cast<uint16_t>(value.value);
            settings::save<pref::PacingLongWordDelay>(preferences, pacing.longWordDelayMs);
            reader.setPacingConfig(pacing);
        }

        ui.label(column.next(12), "Complexity", 1, ui::themes::ColorRole::Muted);
        if (const auto value = ui.slider(column.next(22), reader.pacingConfig().complexWordDelayMs, 0, 600, 50);
            value.changed) {
            auto pacing = reader.pacingConfig();
            pacing.complexWordDelayMs = static_cast<uint16_t>(value.value);
            settings::save<pref::PacingComplexWordDelay>(preferences, pacing.complexWordDelayMs);
            reader.setPacingConfig(pacing);
        }

        ui.label(column.next(12), "Punctuation", 1, ui::themes::ColorRole::Muted);
        if (const auto value = ui.slider(column.next(22), reader.pacingConfig().punctuationDelayMs, 0, 600, 50);
            value.changed) {
            auto pacing = reader.pacingConfig();
            pacing.punctuationDelayMs = static_cast<uint16_t>(value.value);
            settings::save<pref::PacingPunctuationDelay>(preferences, pacing.punctuationDelayMs);
            reader.setPacingConfig(pacing);
        }
    }

} // namespace screens
