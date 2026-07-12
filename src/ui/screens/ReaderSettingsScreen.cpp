#include "ui/screens/ScreenCommon.h"

#include "settings/PreferenceSpecs.h"

namespace screens {

    void readerSettings(ui::Context& ui, ReaderSettings& config, Preferences& preferences, Screen& screen) {
        const ui::Rect content = detail::content(ui);
        if (ui.button({content.x, content.y, 64, 24}, ui.text(UiText::Back)))
            screen = Screen::Settings;
        ui.label({static_cast<int16_t>(content.x + 74), content.y, static_cast<int16_t>(content.w - 74), 24},
                 ui.text(UiText::ReaderScreen), 2);

        constexpr int16_t gap = 5;
        const int16_t sectionsY = static_cast<int16_t>(content.y + 30);
        ui.separator({content.x, sectionsY, content.w, 10}, ui.text(UiText::BehaviorMetricsSection));
        ui::Grid grid{{content.x, static_cast<int16_t>(sectionsY + 14), content.w,
                       static_cast<int16_t>(content.h - 44)}, 2, 32, gap};
        if (ui.setting(grid.next(), ui.text(UiText::ReaderHand),
                       ui.text(config.leftHanded ? UiText::Left : UiText::Right), ui::SettingLayout::Inline)) {
            config.leftHanded = !config.leftHanded;
            settings::save<settings::prefs::Handedness>(preferences, config.leftHanded);
        }
        if (ui.setting(grid.next(), ui.text(UiText::ChapterScroll),
                       ui.text(config.chapterScrollReversed ? UiText::Reversed : UiText::Normal),
                       ui::SettingLayout::Inline))
            config.chapterScrollReversed =
                settings::toggle<settings::prefs::ChapterScrollReversed>(preferences);

        const UiText footer = config.footerMetric == FooterMetric::ChapterTime ? UiText::ChapterTime
                            : config.footerMetric == FooterMetric::BookTime    ? UiText::BookTime
                                                                              : UiText::Percentage;
        if (ui.setting(grid.next(), ui.text(UiText::Footer), ui.text(footer), ui::SettingLayout::Inline)) {
            config.footerMetric = settings::cycle<settings::prefs::FooterMetricMode>(preferences);
        }

        const UiText battery = config.batteryLabel == BatteryLabel::TimeRemaining ? UiText::TimeLeft
                             : config.batteryLabel == BatteryLabel::Voltage       ? UiText::Voltage
                                                                                  : UiText::Percentage;
        if (ui.setting(grid.next(), ui.text(UiText::BatteryLabel), ui.text(battery), ui::SettingLayout::Inline)) {
            config.batteryLabel = settings::cycle<settings::prefs::BatteryLabelMode>(preferences);
        }

        const int16_t visibilityY = static_cast<int16_t>(sectionsY + 84);
        ui.separator({content.x, visibilityY, content.w, 10}, ui.text(UiText::VisibleWhileReadingSection));
        ui::Grid visibility{{content.x, static_cast<int16_t>(visibilityY + 14), content.w,
                             static_cast<int16_t>(content.h - 107)}, 3, 28, gap};
        if (ui.toggle(visibility.next(), ui.text(UiText::Battery), config.batteryVisibleWhileReading))
            config.batteryVisibleWhileReading =
                settings::toggle<settings::prefs::ReaderBatteryVisible>(preferences);
        if (ui.toggle(visibility.next(), ui.text(UiText::Chapter), config.chapterVisibleWhileReading))
            config.chapterVisibleWhileReading =
                settings::toggle<settings::prefs::ReaderChapterVisible>(preferences);
        if (ui.toggle(visibility.next(), ui.text(UiText::Progress), config.progressVisibleWhileReading))
            config.progressVisibleWhileReading =
                settings::toggle<settings::prefs::ReaderProgressVisible>(preferences);
    }

} // namespace screens
