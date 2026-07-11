#include "ui/screens/ScreenCommon.h"

#include "settings/PreferenceSpecs.h"

namespace screens {

    void readerSettings(ui::Context& ui, ReaderSettings& config, Preferences& preferences, Screen& screen) {
        const ui::Rect content = detail::content(ui);
        if (ui.button({content.x, content.y, 64, 24}, ui.text(UiText::Back)))
            screen = Screen::Settings;
        ui.label({static_cast<int16_t>(content.x + 74), content.y, static_cast<int16_t>(content.w - 74), 24},
                 "Reader screen", 2);

        constexpr int16_t gap = 5;
        const int16_t sectionsY = static_cast<int16_t>(content.y + 30);
        ui.separator({content.x, sectionsY, content.w, 10}, "BEHAVIOR & METRICS");
        ui::Grid grid{{content.x, static_cast<int16_t>(sectionsY + 14), content.w,
                       static_cast<int16_t>(content.h - 44)}, 2, 32, gap};
        if (ui.setting(grid.next(), "Reader hand", config.leftHanded ? "Left" : "Right")) {
            config.leftHanded = !config.leftHanded;
            settings::save<settings::prefs::Handedness>(preferences, static_cast<uint8_t>(config.leftHanded));
        }
        if (ui.setting(grid.next(), "Chapter scroll", config.chapterScrollReversed ? "Reversed" : "Normal"))
            config.chapterScrollReversed =
                settings::toggle<settings::prefs::ChapterScrollReversed>(preferences);

        const char* footer = config.footerMetric == FooterMetric::ChapterTime ? "Chapter time"
                           : config.footerMetric == FooterMetric::BookTime    ? "Book time"
                                                                             : "Percentage";
        if (ui.setting(grid.next(), "Footer", footer)) {
            config.footerMetric =
                static_cast<FooterMetric>((static_cast<uint8_t>(config.footerMetric) + 1U) % 3U);
            settings::save<settings::prefs::FooterMetricMode>(preferences,
                                                               static_cast<uint8_t>(config.footerMetric));
        }

        const char* battery = config.batteryLabel == BatteryLabel::TimeRemaining ? "Time left"
                            : config.batteryLabel == BatteryLabel::Voltage       ? "Voltage"
                                                                                : "Percentage";
        if (ui.setting(grid.next(), "Battery label", battery)) {
            config.batteryLabel =
                static_cast<BatteryLabel>((static_cast<uint8_t>(config.batteryLabel) + 1U) % 3U);
            settings::save<settings::prefs::BatteryLabelMode>(preferences,
                                                               static_cast<uint8_t>(config.batteryLabel));
        }

        const int16_t visibilityY = static_cast<int16_t>(sectionsY + 83);
        ui.separator({content.x, visibilityY, content.w, 10}, "VISIBLE WHILE READING");
        ui::Grid visibility{{content.x, static_cast<int16_t>(visibilityY + 14), content.w,
                             static_cast<int16_t>(content.h - 107)}, 3, 28, gap};
        if (ui.toggle(visibility.next(), "Show battery", config.batteryVisibleWhileReading))
            config.batteryVisibleWhileReading =
                settings::toggle<settings::prefs::ReaderBatteryVisible>(preferences);
        if (ui.toggle(visibility.next(), "Show chapter", config.chapterVisibleWhileReading))
            config.chapterVisibleWhileReading =
                settings::toggle<settings::prefs::ReaderChapterVisible>(preferences);
        if (ui.toggle(visibility.next(), "Show progress", config.progressVisibleWhileReading))
            config.progressVisibleWhileReading =
                settings::toggle<settings::prefs::ReaderProgressVisible>(preferences);
    }

} // namespace screens
