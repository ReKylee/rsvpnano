#include "ui/screens/ScreenCommon.h"

#include "settings/PreferenceSpecs.h"

namespace screens {
    namespace pref = settings::prefs;

    void typographySettings(ui::Context& ui, ReaderSettings& config, FontCatalog& fonts, Preferences& preferences,
                            Screen& screen) {
        ui::Column column{detail::content(ui), 3};
        if (ui.button(column.next(20), "Back"))
            screen = Screen::Settings;

        const std::string size = std::string{"Size: "} + FontCatalog::sizeLabel(config.fontSizeIndex);
        if (ui.button(column.next(22), size)) {
            config.fontSizeIndex = settings::cycle<pref::ReaderFontSizeIndex>(preferences, FontCatalog::sizeCount());
            config.font = fonts.loadFont(config.typefaceIndex, config.fontSizeIndex);
        }

        const std::string typeface = std::string{"Typeface: "} + fonts.typefaceLabel(config.typefaceIndex);
        if (ui.button(column.next(22), typeface)) {
            config.typefaceIndex = settings::cycle<pref::ReaderTypefaceIndex>(preferences, fonts.typefaceCount());
            settings::save<pref::ReaderTypefaceId>(preferences, fonts.typefaceId(config.typefaceIndex).c_str());
            config.font = fonts.loadFont(config.typefaceIndex, config.fontSizeIndex);
        }

        const std::string focus =
            std::string{"Focus letter: "} + (config.typography.focusHighlight ? "On" : "Off");
        if (ui.button(column.next(22), focus)) {
            config.typography.focusHighlight = settings::toggle<pref::TypographyFocusHighlight>(preferences);
        }

        if (const auto value = ui.slider(column.next(20), config.typography.tracking, -2, 3, 1); value.changed) {
            config.typography.tracking = static_cast<int8_t>(value.value);
            settings::save<pref::TypographyTracking>(preferences, config.typography.tracking);
        }
        if (const auto value = ui.slider(column.next(20), config.typography.anchor, 30, 40, 1); value.changed) {
            config.typography.anchor = static_cast<uint8_t>(value.value);
            settings::save<pref::TypographyAnchor>(preferences, config.typography.anchor);
        }
        if (const auto value = ui.slider(column.next(20), config.typography.guideWidth, 12, 30, 2); value.changed) {
            config.typography.guideWidth = static_cast<uint8_t>(value.value);
            settings::save<pref::TypographyGuideWidth>(preferences, config.typography.guideWidth);
        }
        if (const auto value = ui.slider(column.next(20), config.typography.guideGap, 2, 8, 1); value.changed) {
            config.typography.guideGap = static_cast<uint8_t>(value.value);
            settings::save<pref::TypographyGuideGap>(preferences, config.typography.guideGap);
        }

        if (ui.button(column.next(22), "Reset typography")) {
            config.fontSizeIndex = pref::ReaderFontSizeIndex::defaultValue();
            config.typefaceIndex = pref::ReaderTypefaceIndex::defaultValue();
            config.typography = {};

            settings::save<pref::ReaderFontSizeIndex>(preferences, config.fontSizeIndex, FontCatalog::sizeCount());
            settings::save<pref::ReaderTypefaceIndex>(preferences, config.typefaceIndex, fonts.typefaceCount());
            settings::save<pref::ReaderTypefaceId>(preferences, fonts.typefaceId(config.typefaceIndex).c_str());
            settings::save<pref::TypographyFocusHighlight>(preferences, config.typography.focusHighlight);
            settings::save<pref::TypographyTracking>(preferences, config.typography.tracking);
            settings::save<pref::TypographyAnchor>(preferences, config.typography.anchor);
            settings::save<pref::TypographyGuideWidth>(preferences, config.typography.guideWidth);
            settings::save<pref::TypographyGuideGap>(preferences, config.typography.guideGap);
            config.font = fonts.loadFont(config.typefaceIndex, config.fontSizeIndex);
        }
    }

} // namespace screens
