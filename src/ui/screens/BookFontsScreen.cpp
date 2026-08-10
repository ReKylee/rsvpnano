#include "ui/screens/ScreenCommon.h"

#include <algorithm>
#include <array>
#include <ranges>
#include <vector>

namespace screens {
    bool bookFonts(ui::Context& ui, const BookMetadata& metadata, settings::ReadingOverrides& overrides,
                   FontCatalog& fonts, Screen& screen) {
        std::array<std::string_view, settings::kMaximumBookLanguages> locales;
        size_t localeCount = 0;
        const auto addLocale = [&](std::string_view locale) {
            const auto existing = std::span{locales}.first(localeCount);
            if (locale.empty() || localeCount == locales.size() || std::ranges::find(existing, locale) != existing.end())
                return;
            locales[localeCount++] = locale;
        };
        addLocale(metadata.locale);
        for (const BookTextRun& run: metadata.textRuns)
            addLocale(run.locale);
        if (localeCount == 0)
            addLocale("und");

        const ui::Rect content = detail::content(ui);
        if (ui.button({content.x, content.y, 64, detail::kBackButtonHeight}, "<<"))
            screen = Screen::Read;
        ui.label({static_cast<int16_t>(content.x + 74), content.y,
                  static_cast<int16_t>(content.w - 164), detail::kBackButtonHeight},
                 ui.text(UiText::Language), 2);
        if (ui.button({static_cast<int16_t>(content.x + content.w - 80), content.y, 80,
                       detail::kBackButtonHeight},
                      ui.text(UiText::Reset))) {
            const bool changed = !overrides.languageFonts.empty();
            overrides.languageFonts.clear();
            return changed;
        }

        const auto families = fonts.families();
        const uint8_t columns = content.w >= 600 ? 3 : content.w >= 400 ? 2 : 1;
        ui::Grid grid{{content.x, static_cast<int16_t>(content.y + detail::kBackButtonHeight + 4), content.w,
                       static_cast<int16_t>(content.h - detail::kBackButtonHeight - 4)},
                      columns, 34, 4};
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

        for (size_t localeIndex = 0; localeIndex < localeCount; ++localeIndex) {
            const std::string_view locale = locales[localeIndex];
            const uint32_t requiredScripts = metadata.scriptsForLocale(locale) & ~UnicodeText::ScriptMath;
            if (requiredScripts == 0)
                continue;

            const auto selected = std::ranges::find(overrides.languageFonts, locale,
                                                    &settings::LanguageFont::locale);
            const std::string_view selectedId = selected == overrides.languageFonts.end()
                                                  ? std::string_view{}
                                                  : std::string_view{selected->fontId};
            const auto nextId = chooseFont(locale, locale, requiredScripts, selectedId);
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
        if ((metadata.scriptMask & UnicodeText::ScriptMath) != 0) {
            const auto selected = std::ranges::find(overrides.languageFonts, settings::kMathFontTarget,
                                                    &settings::LanguageFont::locale);
            const auto nextId = chooseFont("Math", {}, UnicodeText::ScriptMath,
                                           selected == overrides.languageFonts.end()
                                             ? std::string_view{}
                                             : std::string_view{selected->fontId});
            if (nextId) {
                if (nextId->empty()) {
                    if (selected != overrides.languageFonts.end())
                        overrides.languageFonts.erase(selected);
                } else if (selected == overrides.languageFonts.end()) {
                    overrides.languageFonts.push_back({.locale = std::string{settings::kMathFontTarget},
                                                       .fontId = std::move(*nextId)});
                } else {
                    selected->fontId = std::move(*nextId);
                }
                changed = true;
            }
        }
        return changed;
    }

} // namespace screens
