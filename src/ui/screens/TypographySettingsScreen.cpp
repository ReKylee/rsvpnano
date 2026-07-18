#include "ui/screens/ScreenCommon.h"

#include <algorithm>

namespace screens {
    bool typographySettings(ui::Context& ui, std::optional<settings::TypographySettings>& bookOverride,
                            const settings::TypographySettings& inherited, FontCatalog& fonts, Screen& screen) {
        settings::TypographySettings config = bookOverride.value_or(inherited);
        const auto families = fonts.families();
        const auto selected = std::ranges::find_if(families, [&config](const FontCatalog::Family& family) {
            return family.id == config.fontId;
        });
        size_t familyIndex = selected == families.end() ? 0 : selected - families.begin();
        bool changed = false;
        bool reset = false;
        const ui::Rect content = detail::content(ui);
        if (ui.button({content.x, content.y, 64, 24}, ui.text(UiText::Back)))
            screen = Screen::Settings;
        constexpr int16_t resetWidth = 80;
        const int16_t resetX = static_cast<int16_t>(content.x + content.w - resetWidth);
        ui.label({static_cast<int16_t>(content.x + 74), content.y,
                  std::max<int16_t>(0, static_cast<int16_t>(resetX - content.x - 80)), 24},
                 ui.text(UiText::Typography), 2);
        if (ui.button({resetX, content.y, resetWidth, 24}, ui.text(UiText::Reset))) {
            changed = bookOverride.has_value();
            bookOverride.reset();
            config = inherited;
            reset = true;
            const auto inheritedFamily = std::ranges::find_if(families, [&config](const FontCatalog::Family& family) {
                return family.id == config.fontId;
            });
            familyIndex = inheritedFamily == families.end() ? 0 : inheritedFamily - families.begin();
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
            config.fontSizeIndex.cycle();
            changed = true;
        }

        if (ui.setting(font.next(typefaceWidth), ui.text(UiText::Typeface), families[familyIndex].label.c_str())) {
            const size_t next = (familyIndex + 1) % families.size();
            config.fontId = families[next].id;
            familyIndex = next;
            changed = true;
        }

        if (ui.toggle(font.next(focusWidth), ui.text(UiText::Focus), config.focusHighlight)) {
            config.focusHighlight = !config.focusHighlight;
            changed = true;
        }

        const int16_t geometryY = static_cast<int16_t>(fontY + 38);
        ui.separator({content.x, geometryY, content.w, 10}, ui.text(UiText::GeometrySection));
        ui::Grid geometry{{content.x, static_cast<int16_t>(geometryY + 12), content.w,
                           static_cast<int16_t>(content.y + content.h - geometryY - 12)},
                          2,
                          32,
                          4};
        if (const auto value =
                ui.slider(geometry.next(), ui.text(UiText::Tracking), config.tracking,
                          decltype(config.tracking)::min(), decltype(config.tracking)::max(),
                          decltype(config.tracking)::step(), " px");
            value.changed) {
            config.tracking = value.value;
            changed = true;
        }
        if (const auto value =
                ui.slider(geometry.next(), ui.text(UiText::Anchor), config.anchor, decltype(config.anchor)::min(),
                          decltype(config.anchor)::max(), decltype(config.anchor)::step(), "%");
            value.changed) {
            config.anchor = static_cast<uint8_t>(value.value);
            changed = true;
        }
        if (const auto value =
                ui.slider(geometry.next(), ui.text(UiText::GuideWidth), config.guideWidth,
                          decltype(config.guideWidth)::min(), decltype(config.guideWidth)::max(),
                          decltype(config.guideWidth)::step(), " px");
            value.changed) {
            config.guideWidth = static_cast<uint8_t>(value.value);
            changed = true;
        }
        if (const auto value =
                ui.slider(geometry.next(), ui.text(UiText::GuideGap), config.guideGap,
                          decltype(config.guideGap)::min(), decltype(config.guideGap)::max(),
                          decltype(config.guideGap)::step(), " px");
            value.changed) {
            config.guideGap = static_cast<uint8_t>(value.value);
            changed = true;
        }
        if (changed && !reset)
            bookOverride = std::move(config);
        return changed;
    }

} // namespace screens
