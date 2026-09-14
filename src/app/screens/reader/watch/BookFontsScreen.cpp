#include "app/screens/reader/BookFontChoices.h"
#include "app/screens/watch/Layout.h"

namespace screens {
    bool bookFonts(ui::Context& ui, const BookMetadata& metadata, settings::ReadingOverrides& overrides,
                   const locales::Catalog& localeCatalog, FontCatalog& fonts, Screen& screen) {
        const size_t languageCount = bookFontTargetCount(metadata);

        auto area = watch::header(ui, ui.text(UiText::Typeface), Screen::Read, screen);
        const auto page = ui.pagedGrid(area, languageCount + 1, 1, 56);
        size_t row = 0;
        if (ui.card(page.item(row++), ui.text(UiText::Reset), {}, watch::textSize(ui))) {
            const bool changed = !overrides.languageFonts.empty();
            overrides.languageFonts.clear();
            return changed;
        }

        return editBookFonts(ui, metadata, overrides, localeCatalog, fonts, [&] { return page.item(row++); },
                             [&](ui::Rect rect, std::string_view label, std::string_view value) {
                                 return ui.card(rect, label, value, watch::textSize(ui));
                             });
    }

} // namespace screens
