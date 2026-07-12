#include "ui/screens/ScreenCommon.h"

#include <algorithm>

#include "settings/PreferenceSpecs.h"

namespace screens {
    namespace pref = settings::prefs;

    void typographySettings(ui::Context& ui, ReaderSettings& config, FontCatalog& fonts, Preferences& preferences,
                            Screen& screen) {
        const ui::Rect content = detail::content(ui);
        if (ui.button({content.x, content.y, 64, 24}, ui.text(UiText::Back)))
            screen = Screen::Settings;
        constexpr int16_t resetWidth = 80;
        const int16_t resetX = static_cast<int16_t>(content.x + content.w - resetWidth);
        ui.label({static_cast<int16_t>(content.x + 74), content.y,
                  std::max<int16_t>(0, static_cast<int16_t>(resetX - content.x - 80)), 24},
                 ui.text(UiText::Typography), 2);
        if (ui.button({resetX, content.y, resetWidth, 24}, ui.text(UiText::Reset))) {
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

        constexpr int16_t gap = 6;
        const int16_t sectionsY = static_cast<int16_t>(content.y + 26);
        ui.separator({content.x, sectionsY, content.w, 10}, ui.text(UiText::FontSection));
        const int16_t fontY = static_cast<int16_t>(sectionsY + 12);
        const int16_t availableWidth = static_cast<int16_t>(content.w - gap * 2);
        const int16_t fontSizeWidth = content.w >= 480 ? 190 : static_cast<int16_t>(availableWidth / 3);
        const int16_t focusWidth = content.w >= 480 ? 150 : static_cast<int16_t>(availableWidth / 3);
        const int16_t typefaceWidth = static_cast<int16_t>(content.w - fontSizeWidth - focusWidth - gap * 2);
        ui::Row font{{content.x, fontY, content.w, 34}, gap};

        if (ui.setting(font.next(fontSizeWidth), ui.text(UiText::FontSize),
                       FontCatalog::sizeLabel(config.fontSizeIndex), ui::SettingLayout::Inline)) {
            config.fontSizeIndex = settings::cycle<pref::ReaderFontSizeIndex>(preferences);
            config.font = fonts.loadFont(config.typefaceIndex, config.fontSizeIndex);
        }

        if (ui.setting(font.next(typefaceWidth), ui.text(UiText::Typeface), fonts.typefaceLabel(config.typefaceIndex))) {
            if (fonts.typefaceCount() == 0)
                return;
            config.typefaceIndex = static_cast<uint8_t>((config.typefaceIndex + 1U) % fonts.typefaceCount());
            settings::save<pref::ReaderTypefaceId>(preferences, fonts.typefaceId(config.typefaceIndex).c_str());
            config.font = fonts.loadFont(config.typefaceIndex, config.fontSizeIndex);
        }

        if (ui.toggle(font.next(focusWidth), ui.text(UiText::Focus), config.typography.focusHighlight)) {
            config.typography.focusHighlight = settings::toggle<pref::TypographyFocusHighlight>(preferences);
        }

        const int16_t geometryY = static_cast<int16_t>(fontY + 38);
        ui.separator({content.x, geometryY, content.w, 10}, ui.text(UiText::GeometrySection));
        ui::Grid geometry{{content.x, static_cast<int16_t>(geometryY + 12), content.w,
                           static_cast<int16_t>(content.y + content.h - geometryY - 12)},
                          2, 32, 4};
        if (const auto value = ui.slider(geometry.next(), ui.text(UiText::Tracking), config.typography.tracking, -2,
                                         3, 1, " px");
            value.changed) {
            config.typography.tracking = static_cast<int8_t>(value.value);
            settings::save<pref::TypographyTracking>(preferences, config.typography.tracking);
        }
        if (const auto value = ui.slider(geometry.next(), ui.text(UiText::Anchor), config.typography.anchor, 30, 40,
                                         1, "%");
            value.changed) {
            config.typography.anchor = static_cast<uint8_t>(value.value);
            settings::save<pref::TypographyAnchor>(preferences, config.typography.anchor);
        }
        if (const auto value =
                ui.slider(geometry.next(), ui.text(UiText::GuideWidth), config.typography.guideWidth, 12, 30, 2,
                          " px");
            value.changed) {
            config.typography.guideWidth = static_cast<uint8_t>(value.value);
            settings::save<pref::TypographyGuideWidth>(preferences, config.typography.guideWidth);
        }
        if (const auto value = ui.slider(geometry.next(), ui.text(UiText::GuideGap), config.typography.guideGap, 2,
                                         8, 1, " px");
            value.changed) {
            config.typography.guideGap = static_cast<uint8_t>(value.value);
            settings::save<pref::TypographyGuideGap>(preferences, config.typography.guideGap);
        }

    }

} // namespace screens
