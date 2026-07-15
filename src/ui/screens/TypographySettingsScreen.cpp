#include "ui/screens/ScreenCommon.h"

#include <algorithm>

#include "settings/PreferenceSpecs.h"

namespace screens {
    namespace pref = settings::prefs;

    bool typographySettings(ui::Context& ui, ReaderSettings& config, FontCatalog& fonts, ThemeStore& themes,
                            Preferences& preferences, Screen& screen) {
        const auto families = fonts.families();
        const auto selected = std::ranges::find_if(families, [&themes](const FontCatalog::Family& family) {
            return family.id == themes.selected().typeface;
        });
        size_t familyIndex = selected == families.end() ? 0 : selected - families.begin();
        bool fontChanged = false;
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
            config.typography = {};

            settings::save<pref::ReaderFontSizeIndex>(preferences, config.fontSizeIndex);
            if (themes.setSelectedTypeface(families.front().id, fonts))
                familyIndex = 0;
            settings::save<pref::TypographyFocusHighlight>(preferences, config.typography.focusHighlight);
            settings::save<pref::TypographyTracking>(preferences, config.typography.tracking);
            settings::save<pref::TypographyAnchor>(preferences, config.typography.anchor);
            settings::save<pref::TypographyGuideWidth>(preferences, config.typography.guideWidth);
            settings::save<pref::TypographyGuideGap>(preferences, config.typography.guideGap);
            fontChanged = true;
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

        if (ui.setting(font.next(fontSizeWidth), ui.text(UiText::FontSize), RFont4::sizeLabel(config.fontSizeIndex),
                       ui::SettingLayout::Inline)) {
            config.fontSizeIndex = settings::cycle<pref::ReaderFontSizeIndex>(preferences);
            fontChanged = true;
        }

        if (ui.setting(font.next(typefaceWidth), ui.text(UiText::Typeface), families[familyIndex].label.c_str())) {
            const size_t next = (familyIndex + 1) % families.size();
            if (themes.setSelectedTypeface(families[next].id, fonts)) {
                familyIndex = next;
                fontChanged = true;
            }
        }

        if (ui.toggle(font.next(focusWidth), ui.text(UiText::Focus), config.typography.focusHighlight)) {
            config.typography.focusHighlight = settings::toggle<pref::TypographyFocusHighlight>(preferences);
        }

        const int16_t geometryY = static_cast<int16_t>(fontY + 38);
        ui.separator({content.x, geometryY, content.w, 10}, ui.text(UiText::GeometrySection));
        ui::Grid geometry{{content.x, static_cast<int16_t>(geometryY + 12), content.w,
                           static_cast<int16_t>(content.y + content.h - geometryY - 12)},
                          2,
                          32,
                          4};
        if (const auto value =
                ui.slider(geometry.next(), ui.text(UiText::Tracking), config.typography.tracking, -2, 3, 1, " px");
            value.changed) {
            config.typography.tracking = static_cast<int8_t>(value.value);
            settings::save<pref::TypographyTracking>(preferences, config.typography.tracking);
        }
        if (const auto value =
                ui.slider(geometry.next(), ui.text(UiText::Anchor), config.typography.anchor, 30, 40, 1, "%");
            value.changed) {
            config.typography.anchor = static_cast<uint8_t>(value.value);
            settings::save<pref::TypographyAnchor>(preferences, config.typography.anchor);
        }
        if (const auto value =
                ui.slider(geometry.next(), ui.text(UiText::GuideWidth), config.typography.guideWidth, 12, 30, 2, " px");
            value.changed) {
            config.typography.guideWidth = static_cast<uint8_t>(value.value);
            settings::save<pref::TypographyGuideWidth>(preferences, config.typography.guideWidth);
        }
        if (const auto value =
                ui.slider(geometry.next(), ui.text(UiText::GuideGap), config.typography.guideGap, 2, 8, 1, " px");
            value.changed) {
            config.typography.guideGap = static_cast<uint8_t>(value.value);
            settings::save<pref::TypographyGuideGap>(preferences, config.typography.guideGap);
        }
        return fontChanged;
    }

} // namespace screens
