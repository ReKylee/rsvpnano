#include "ui/screens/ScreenCommon.h"

#include <algorithm>
#include <ranges>
#include <vector>

namespace screens {
    bool bookFonts(ui::Context& ui, const BookMetadata& metadata, settings::ReadingOverrides& overrides,
                   const locales::Catalog& localeCatalog, FontCatalog& fonts, Screen& screen) {
        std::vector<std::string_view> locales;
        locales.reserve(metadata.textRuns.size() + 1);
        const auto addLocale = [&](std::string_view locale) {
            if (locale.empty() || std::ranges::find(locales, locale) != locales.end())
                return;
            locales.push_back(locale);
        };
        addLocale(metadata.locale);
        for (const BookTextRun& run: metadata.textRuns)
            addLocale(run.locale);
        if (locales.empty())
            addLocale("und");

        const ui::Rect content = detail::content(ui);
        std::vector<std::pair<std::string_view, uint32_t>> targets;
        targets.reserve(locales.size() + 1);
        for (const std::string_view locale: locales) {
            const uint32_t scripts = metadata.scriptsForLocale(locale) & ~UnicodeText::ScriptMath;
            if (scripts != 0)
                targets.emplace_back(locale, scripts);
        }
        if ((metadata.scriptMask & UnicodeText::ScriptMath) != 0)
            targets.emplace_back(settings::kMathFontTarget, UnicodeText::ScriptMath);

        constexpr int16_t gap = 4;
        const uint8_t columns = content.w >= 600 ? 4 : 3;
        const size_t itemCount = targets.size() + 2;
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

        const auto families = fonts.families();
        bool changed = false;
        const auto chooseFont = [&](std::string_view label, std::string_view locale, uint32_t requiredScripts,
                                    std::string_view selectedId) -> std::optional<std::string> {
            std::vector<size_t> compatible;
            compatible.reserve(families.size());
            for (size_t familyIndex = 0; familyIndex < families.size(); ++familyIndex) {
                if (families[familyIndex].usableFor(locale, requiredScripts))
                    compatible.push_back(familyIndex);
            }

            auto active = std::ranges::find_if(compatible,
                                               [&](size_t index) { return families[index].id == selectedId; });
            const std::string_view value = selectedId.empty() || active == compatible.end()
                                             ? ui.text(UiText::Default)
                                             : std::string_view{families[*active].label};
            if (!ui.setting(grid.next(), label, value, ui::SettingLayout::Inline) || compatible.empty())
                return std::nullopt;
            if (selectedId.empty() || active == compatible.end())
                return families[compatible.front()].id;
            ++active;
            return active == compatible.end() ? std::string{} : families[*active].id;
        };

        for (const auto& [locale, requiredScripts]: targets) {
            const auto selected = std::ranges::find(overrides.languageFonts, locale,
                                                    &settings::LanguageFont::locale);
            const std::string_view selectedId = selected == overrides.languageFonts.end()
                                                  ? std::string_view{}
                                                  : std::string_view{selected->fontId};
            const bool math = locale == settings::kMathFontTarget;
            const auto nextId = chooseFont(math ? std::string_view{"Math"} : locales::localeName(localeCatalog, locale),
                                           math ? std::string_view{} : locale, requiredScripts, selectedId);
            if (!nextId)
                continue;
            if (nextId->empty()) {
                if (selected != overrides.languageFonts.end())
                    overrides.languageFonts.erase(selected);
            } else if (selected == overrides.languageFonts.end()) {
                overrides.languageFonts.push_back({.locale = std::string{locale},
                                                   .fontId = std::move(*nextId)});
            } else {
                selected->fontId = std::move(*nextId);
            }
            changed = true;
        }
        return changed;
    }

} // namespace screens
