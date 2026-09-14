#include "app/screens/reader/BookFontChoices.h"
#include "app/screens/ScreenCommon.h"

namespace screens {
    bool bookFonts(ui::Context& ui, const BookMetadata& metadata, settings::ReadingOverrides& overrides,
                   const locales::Catalog& localeCatalog, FontCatalog& fonts, Screen& screen) {
        const ui::Rect content = detail::content(ui);
        const size_t languageCount = bookFontTargetCount(metadata);

        constexpr int16_t gap = 4;
        const uint8_t columns = content.w >= 600 ? 4 : 3;
        const size_t itemCount = languageCount + 2;
        const size_t rows = (itemCount + columns - 1) / columns;
        const int16_t rowHeight = std::min<int16_t>(34, static_cast<int16_t>(
            (content.h - gap * static_cast<int16_t>(rows - 1)) / static_cast<int16_t>(rows)));
        ui::Grid grid{content, columns, rowHeight, gap};
        if (ui.button(grid.next(), "<<"))
            screen = Screen::Read;
        if (ui.button(grid.next(), ui.text(UiText::Reset))) {
            const bool changed = !overrides.languageFonts.empty();
            overrides.languageFonts.clear();
            return changed;
        }

        return editBookFonts(ui, metadata, overrides, localeCatalog, fonts, [&] { return grid.next(); },
                             [&](ui::Rect rect, std::string_view label, std::string_view value) {
                                 return ui.setting(rect, label, value, ui::SettingLayout::Inline);
                             });
    }

} // namespace screens
