#pragma once

#include <algorithm>
#include <optional>
#include "app/screens/reader/BookFontsScreen.h"

namespace screens {
    template<typename Visit>
    void forEachBookFontTarget(const BookMetadata& metadata, Visit&& visit) {
        bool hasLanguage = false;
        metadata.forEachLanguage([&](std::string_view locale, uint32_t scripts) {
            scripts &= ~UnicodeText::ScriptMath;
            if (scripts != 0) {
                hasLanguage = true;
                visit(locale, scripts);
            }
        });
        if (!hasLanguage && (metadata.scriptMask & ~UnicodeText::ScriptMath) != 0)
            visit("und", metadata.scriptMask & ~UnicodeText::ScriptMath);
        if ((metadata.scriptMask & UnicodeText::ScriptMath) != 0)
            visit(settings::kMathFontTarget, UnicodeText::ScriptMath);
    }

    inline size_t bookFontTargetCount(const BookMetadata& metadata) {
        size_t count = 0;
        forEachBookFontTarget(metadata, [&](std::string_view, uint32_t) { ++count; });
        return count;
    }

    // Geometry is requested first so an off-page target performs no font lookup or widget work.
    template<typename NextRect, typename Draw>
    bool editBookFonts(ui::Context& ui, const BookMetadata& metadata, settings::ReadingOverrides& overrides,
                       const locales::Catalog& locales, FontCatalog& fonts, NextRect&& nextRect, Draw&& draw) {
        const auto families = fonts.families();
        bool changed = false;
        forEachBookFontTarget(metadata, [&](std::string_view locale, uint32_t scripts) {
            const ui::Rect rect = nextRect();
            if (rect.w <= 0 || rect.h <= 0)
                return;
            const auto selected = std::ranges::find(overrides.languageFonts, locale, &settings::LanguageFont::locale);
            const std::string_view selectedId = selected == overrides.languageFonts.end()
                                                   ? std::string_view{} : selected->fontId;
            const bool math = locale == settings::kMathFontTarget;
            const std::string_view fontLocale = math ? std::string_view{} : locale;
            size_t active = families.size();
            for (size_t i = 0; i < families.size(); ++i)
                if (families[i].id == selectedId && families[i].usableFor(fontLocale, scripts)) {
                    active = i;
                    break;
                }
            const std::string_view value = selectedId.empty() || active == families.size()
                                               ? ui.text(UiText::Default) : families[active].label;
            const auto label = math ? std::string_view{"Math"} : locales::localeName(locales, locale);
            if (!draw(rect, label, value))
                return;
            size_t next = active == families.size() ? 0 : active + 1;
            while (next < families.size() && !families[next].usableFor(fontLocale, scripts))
                ++next;
            if (next == families.size()) {
                if (active == families.size())
                    return;
                if (selected != overrides.languageFonts.end())
                    overrides.languageFonts.erase(selected);
            } else if (selected == overrides.languageFonts.end()) {
                overrides.languageFonts.push_back({.locale = std::string{locale}, .fontId = families[next].id});
            } else {
                selected->fontId = families[next].id;
            }
            changed = true;
        });
        return changed;
    }
} // namespace screens
