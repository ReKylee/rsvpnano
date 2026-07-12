#include "ui/screens/ScreenCommon.h"

#include "settings/PreferenceSpecs.h"

namespace screens {
    namespace pref = settings::prefs;

    void typographySettings(ui::Context& ui, ReaderSettings& config, FontCatalog& fonts, Preferences& preferences,
                            Screen& screen) {
        const ui::Rect content = detail::content(ui);
        if (ui.button({content.x, content.y, 64, 24}, ui.text(UiText::Back)))
            screen = Screen::Settings;
        constexpr int16_t resetWidth = 132;
        const int16_t resetX = static_cast<int16_t>(content.x + content.w - resetWidth);
        ui.label({static_cast<int16_t>(content.x + 74), content.y,
                  static_cast<int16_t>(resetX - content.x - 80), 24},
                 ui.text(UiText::Typography), 2);
        if (ui.button({resetX, content.y, resetWidth, 24}, ui.text(UiText::ResetTypography))) {
            config.fontSizeIndex = pref::ReaderFontSizeIndex::defaultValue();
            config.typefaceIndex = 0;
            config.typography = {};

            settings::save<pref::ReaderFontSizeIndex>(preferences, config.fontSizeIndex);
            settings::save<pref::ReaderTypefaceId>(preferences, fonts.typefaceId(config.typefaceIndex).c_str());
            settings::save<pref::TypographyFocusHighlight>(preferences, config.typography.focusHighlight);
            settings::save<pref::TypographyTracking>(preferences, config.typography.tracking);
            settings::save<pref::TypographyAnchor>(preferences, config.typography.anchor);
            settings::save<pref::TypographyGuideWidth>(preferences, config.typography.guideWidth);
            settings::save<pref::TypographyGuideGap>(preferences, config.typography.guideGap);
            config.font = fonts.loadFont(config.typefaceIndex, config.fontSizeIndex);
        }

        const int16_t gap = 6;
        const int16_t columnWidth = static_cast<int16_t>((content.w - gap) / 2);
        const int16_t sectionsY = static_cast<int16_t>(content.y + 30);
        ui.separator({content.x, sectionsY, columnWidth, 10}, ui.text(UiText::FontSection));
        ui.separator({static_cast<int16_t>(content.x + columnWidth + gap), sectionsY, columnWidth, 10},
                     ui.text(UiText::GeometrySection));
        ui::Column font{{content.x, static_cast<int16_t>(sectionsY + 14), columnWidth,
                         static_cast<int16_t>(content.h - 44)}, 5};
        ui::Column geometry{{static_cast<int16_t>(content.x + columnWidth + gap),
                             static_cast<int16_t>(sectionsY + 14), columnWidth,
                             static_cast<int16_t>(content.h - 44)}, 4};

        if (ui.setting(font.next(32), ui.text(UiText::FontSize), FontCatalog::sizeLabel(config.fontSizeIndex))) {
            config.fontSizeIndex = settings::cycle<pref::ReaderFontSizeIndex>(preferences);
            config.font = fonts.loadFont(config.typefaceIndex, config.fontSizeIndex);
        }

        if (ui.setting(font.next(32), ui.text(UiText::Typeface), fonts.typefaceLabel(config.typefaceIndex))) {
            if (fonts.typefaceCount() == 0)
                return;
            config.typefaceIndex = static_cast<uint8_t>((config.typefaceIndex + 1U) % fonts.typefaceCount());
            settings::save<pref::ReaderTypefaceId>(preferences, fonts.typefaceId(config.typefaceIndex).c_str());
            config.font = fonts.loadFont(config.typefaceIndex, config.fontSizeIndex);
        }

        if (ui.toggle(font.next(32), ui.text(UiText::FocusLetter), config.typography.focusHighlight)) {
            config.typography.focusHighlight = settings::toggle<pref::TypographyFocusHighlight>(preferences);
        }

        if (const auto value = ui.slider(geometry.next(25), ui.text(UiText::Tracking), config.typography.tracking,
                                         -2, 3, 1, " px");
            value.changed) {
            config.typography.tracking = static_cast<int8_t>(value.value);
            settings::save<pref::TypographyTracking>(preferences, config.typography.tracking);
        }
        if (const auto value = ui.slider(geometry.next(25), ui.text(UiText::Anchor), config.typography.anchor, 30, 40,
                                         1, "%");
            value.changed) {
            config.typography.anchor = static_cast<uint8_t>(value.value);
            settings::save<pref::TypographyAnchor>(preferences, config.typography.anchor);
        }
        if (const auto value =
                ui.slider(geometry.next(25), ui.text(UiText::GuideWidth), config.typography.guideWidth, 12, 30, 2,
                          " px");
            value.changed) {
            config.typography.guideWidth = static_cast<uint8_t>(value.value);
            settings::save<pref::TypographyGuideWidth>(preferences, config.typography.guideWidth);
        }
        if (const auto value = ui.slider(geometry.next(25), ui.text(UiText::GuideGap), config.typography.guideGap, 2,
                                         8, 1, " px");
            value.changed) {
            config.typography.guideGap = static_cast<uint8_t>(value.value);
            settings::save<pref::TypographyGuideGap>(preferences, config.typography.guideGap);
        }

    }

} // namespace screens
