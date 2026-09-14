#include "ui/Ui.h"

#if __has_include(<esp_log.h>)
#include <esp_log.h>
#endif

#include "fonts/UiFont6x9.h"
#include "text/UnicodeText.h"
#include "text/Utf8Text.h"

namespace ui {
    namespace {
        constexpr uint8_t kUiFontCellWidth = 6;
        constexpr uint8_t kUiFontHeight = 9;
        constexpr uint32_t kBuiltInUiScripts = UnicodeText::ScriptLatin | UnicodeText::ScriptCyrillic;
    } // namespace

    int16_t Context::textWidth(std::string_view text, uint8_t size) {
        const int32_t width =
            static_cast<int32_t>(Utf8Text::count(text)) * kUiFontCellWidth * std::max<uint8_t>(1, size);
        return static_cast<int16_t>(std::min<int32_t>(width, INT16_MAX));
    }

    int16_t Context::textHeight(uint8_t size) {
        return static_cast<int16_t>(kUiFontHeight * std::max<uint8_t>(1, size));
    }

    const locales::UiAssets* Context::fontAssetsFor(std::string_view text, std::string_view textLocale) const {
        if (languageAssets_.owns(text) && !languageAssets_.font.empty())
            return &languageAssets_;
        const uint32_t requiredScripts = UnicodeText::scriptsIn(text) & ~kBuiltInUiScripts;
        if (requiredScripts == 0 || languageFilesystem_ == nullptr || languageCatalog_ == nullptr
            || languageFontLoader_ == nullptr)
            return nullptr;
        const std::string_view preferredLocale = textLocale.empty() ? std::string_view{locale_} : textLocale;
        const locales::InstalledPack* pack =
            locales::findPackForScripts(*languageCatalog_, preferredLocale, requiredScripts);
        if (pack == nullptr)
            return nullptr;
        if (pack->locale == locale_ && !languageAssets_.font.empty())
            return &languageAssets_;
        const auto cached = std::ranges::find(contentFonts_, pack->id, [](const auto& entry) -> const std::string& {
            return entry.first;
        });
        if (cached != contentFonts_.end())
            return cached->second.font.empty() ? nullptr : &cached->second;

        auto& [id, assets] = contentFonts_.emplace_back(pack->id, locales::UiAssets{.direction = pack->direction});
        auto loaded = languageFontLoader_(*languageFilesystem_, *pack);
        if (loaded)
            assets.font = std::move(*loaded);
#if __has_include(<esp_log.h>)
        else
            ESP_LOGW("ui", "content font unavailable pack=%s: %s", id.c_str(), loaded.error().c_str());
#endif
        return assets.font.empty() ? nullptr : &assets;
    }

    int16_t Context::textWidthFor(std::string_view text, uint8_t size, std::string_view textLocale) const {
        const locales::UiAssets* assets = fontAssetsFor(text, textLocale);
        const uint8_t cellWidth = assets == nullptr ? kUiFontCellWidth : locales::uiFontCellWidth(assets->font);
        const int32_t width = static_cast<int32_t>(Utf8Text::count(text)) * cellWidth * std::max<uint8_t>(1, size);
        return static_cast<int16_t>(std::min<int32_t>(width, INT16_MAX));
    }

    int16_t Context::textHeightFor(std::string_view text, uint8_t size, std::string_view textLocale) const {
        const locales::UiAssets* assets = fontAssetsFor(text, textLocale);
        const uint8_t height = assets == nullptr ? kUiFontHeight : locales::uiFontHeight(assets->font);
        return static_cast<int16_t>(height * std::max<uint8_t>(1, size));
    }

    void Context::setLanguageCatalog(fs::FS* filesystem, const locales::Catalog* catalog,
                                     LanguageFontLoader fontLoader) {
        languageFilesystem_ = filesystem;
        languageCatalog_ = catalog;
        languageFontLoader_ = fontLoader;
        contentFonts_.clear();
        invalidate();
    }

    void Context::setLanguageAssets(locales::UiAssets assets) {
        languageAssets_ = std::move(assets);
        contentFonts_.clear();
        invalidate();
    }

    void Context::setLocale(std::string_view locale) {
        if (locale.empty())
            locale = Localization::kDefaultLocale;
        if (locale_ != locale) {
            locale_ = locale;
            invalidate();
        }
    }

    std::string_view Context::text(UiText key) const {
        const std::string_view translated = languageAssets_.text(static_cast<size_t>(key));
        if (!translated.empty())
            return translated;
        return Localization::text(key);
    }

    void Context::prepareTextFont(std::string_view text, std::string_view textLocale) const {
        (void) fontAssetsFor(text, textLocale);
    }

    void Context::drawText(Rect rect, std::string_view text, uint8_t textSize, uint16_t textColor, TextAlign align,
                           uint8_t maxLines, std::string_view textLocale) {
        rect = paintBounds(rect);
        const auto layout = prepareText(rect, text, textSize, align, maxLines, textLocale);
        paint(rect, [&](Arduino_GFX& output, Rect translated) {
            drawText(output, layout, textColor, translated.x - rect.x, translated.y - rect.y);
        });
    }

    TextLayout Context::prepareText(Rect rect, std::string_view text, uint8_t textSize, TextAlign align,
                                    uint8_t maxLines, std::string_view textLocale) {
        TextLayout layout;
        if (rect.w <= 0 || rect.h <= 0 || text.empty())
            return layout;
        layout.lines.reserve(std::min<uint8_t>(maxLines, 2));
        appendText(layout, rect, text, textSize, align, maxLines, textLocale);
        return layout;
    }

    void Context::appendText(TextLayout& layout, Rect rect, std::string_view text, uint8_t textSize, TextAlign align,
                             uint8_t maxLines, std::string_view textLocale) {
        if (rect.w <= 0 || rect.h <= 0 || text.empty())
            return;
        const locales::UiAssets* assets = fontAssetsFor(text, textLocale);
        const bool externalFont = assets != nullptr;
        const uint8_t cellWidth = externalFont ? locales::uiFontCellWidth(assets->font) : kUiFontCellWidth;
        const uint8_t fontHeight = externalFont ? locales::uiFontHeight(assets->font) : kUiFontHeight;
        const size_t codepoints = Utf8Text::count(text);
        uint8_t size = std::max<uint8_t>(1, textSize);
        while (size > 1) {
            const size_t columns = static_cast<size_t>(rect.w) / (cellWidth * size);
            const size_t lines = columns == 0 ? SIZE_MAX : (codepoints + columns - 1) / columns;
            if (lines <= maxLines && static_cast<size_t>(fontHeight) * size * lines <= static_cast<size_t>(rect.h))
                break;
            --size;
        }
        const size_t capacity = static_cast<size_t>(std::max<int16_t>(0, rect.w) / (cellWidth * size));
        if (capacity == 0)
            return;
        const uint8_t* font = externalFont ? assets->font.data() : u8g2_font_rsvpnano_ui_6x9_tf;
        gfx_.setFont(font);
        gfx_.setUTF8Print(true);
        gfx_.setTextSize(size);
        gfx_.setTextWrap(false);
        const bool rightToLeft = (externalFont ? assets->direction : languageAssets_.direction) == TextDirection::rtl;
        if (align == TextAlign::Start)
            align = rightToLeft ? TextAlign::Right : TextAlign::Left;

        std::string_view first = text;
        std::string_view second;
        if (maxLines > 1 && codepoints > capacity) {
            size_t split = Utf8Text::prefixBytes(text, capacity);
            const size_t space = text.rfind(' ', split);
            if (space != std::string_view::npos && Utf8Text::count(text.substr(0, space)) >= capacity / 2)
                split = space;
            first = text.substr(0, split);
            second = text.substr(split);
            while (!second.empty() && second.front() == ' ')
                second.remove_prefix(1);
        }

        const uint8_t lineCount = second.empty() ? 1 : 2;
        const int16_t lineHeight = static_cast<int16_t>(fontHeight * size);
        const int16_t firstY =
            static_cast<int16_t>(rect.y + std::max<int16_t>(0, (rect.h - lineHeight * lineCount) / 2));
        const auto appendLine = [&](std::string_view line, size_t lineCodepoints, int16_t y) {
            const bool truncated = lineCodepoints > capacity;
            const size_t length = truncated && capacity > 3 ? Utf8Text::prefixBytes(line, capacity - 3)
                                : truncated                 ? 0
                                                            : line.size();
            const size_t dots = truncated ? std::min<size_t>(3, capacity) : 0;
            const std::string_view visible = line.substr(0, length);
            const bool bidiReady = rightToLeft && bidiAnalysis_.reset(visible, TextDirection::rtl)
                                && bidiAnalysis_.resolve({0, visible.size()}, bidiLine_);
            if (bidiReady)
                BidiText::visualCodepoints(visible, bidiLine_, bidiCodepoints_);

            const auto appendVisual = [&](std::string& output) {
                if (!bidiReady) {
                    output.append(visible);
                    return;
                }
                std::array<char, 4> encoded{};
                for (const BidiText::Codepoint& codepoint: bidiCodepoints_)
                    output.append(encoded.data(), Utf8Text::encode(codepoint.value, encoded));
            };

            std::string rendered;
            rendered.reserve(visible.size() + dots);
            if (rightToLeft)
                rendered.append(dots, '.');
            appendVisual(rendered);
            if (!rightToLeft)
                rendered.append(dots, '.');

            int16_t inkX = 0;
            int16_t inkY = 0;
            uint16_t inkWidth = 0;
            uint16_t inkHeight = 0;
            gfx_.getTextBounds(rendered.c_str(), 0, 0, &inkX, &inkY, &inkWidth, &inkHeight);
            const int16_t left = static_cast<int16_t>(rect.x - inkX);
            const int16_t x = align == TextAlign::Center
                                ? std::max<int16_t>(left, static_cast<int16_t>(rect.x + (rect.w - inkWidth) / 2 - inkX))
                            : align == TextAlign::Right
                                ? std::max<int16_t>(left, static_cast<int16_t>(rect.x + rect.w - inkWidth - inkX))
                                : left;
            const int16_t baseline = static_cast<int16_t>(y + lineHeight - size);
            // A literal newline returns to the viewport edge, not this line's cursor origin.
            const Rect ink = rendered.find('\n') == std::string::npos
                               ? Rect{static_cast<int16_t>(x + inkX), static_cast<int16_t>(baseline + inkY),
                                      static_cast<int16_t>(inkWidth), static_cast<int16_t>(inkHeight)}
                               : Rect{};
            layout.lines.push_back({std::move(rendered), font, size, x, baseline, ink});
        };
        appendLine(first, second.empty() ? codepoints : Utf8Text::count(first), firstY);
        if (!second.empty())
            appendLine(second, Utf8Text::count(second), static_cast<int16_t>(firstY + lineHeight));
    }

    void Context::drawText(Arduino_GFX& output, const TextLayout& layout, uint16_t textColor, int16_t dx, int16_t dy) {
        if (layout.lines.empty())
            return;
        output.setUTF8Print(true);
        output.setTextWrap(false);
        output.setTextColor(textColor);
        for (const auto& line: layout.lines) {
            const Rect ink{static_cast<int16_t>(line.ink.x + dx), static_cast<int16_t>(line.ink.y + dy), line.ink.w,
                           line.ink.h};
            const Rect visible = intersection(ink, {0, 0, output.width(), output.height()});
            if (line.ink.w > 0 && line.ink.h > 0 && (visible.w <= 0 || visible.h <= 0))
                continue;
            output.setFont(line.font);
            output.setTextSize(line.size);
            output.setCursor(static_cast<int16_t>(line.x + dx), static_cast<int16_t>(line.y + dy));
            for (const char byte: line.text)
                output.write(static_cast<uint8_t>(byte));
        }
        drew_ = true;
    }

    size_t Context::fixedText(Rect rect, std::string_view text, uint8_t textSize, uint16_t ink, TextAlign align,
                              uint8_t maxLines, bool ellipsis) {
        rect = paintBounds(rect);
        const auto layout = prepareFixedText(rect, text, textSize, align, maxLines, ellipsis);
        paint(rect, [&](Arduino_GFX& output, Rect translated) {
            drawText(output, layout, ink, translated.x - rect.x, translated.y - rect.y);
        });
        return layout.consumed;
    }

    TextLayout Context::prepareFixedText(Rect rect, std::string_view text, uint8_t textSize, TextAlign align,
                                         uint8_t maxLines, bool ellipsis) {
        TextLayout layout;
        if (rect.w <= 0 || rect.h <= 0 || text.empty())
            return layout;
        const size_t originalLength = text.size();
        textSize = std::max<uint8_t>(1, textSize);
        const auto* assets = fontAssetsFor(text);
        const int cell = (assets ? locales::uiFontCellWidth(assets->font) : 6) * textSize;
        const int height = textHeightFor(text, textSize);
        const size_t capacity = rect.w / std::max(1, cell);
        const int lines = std::min<int>(maxLines, rect.h / std::max(1, height));
        if (!capacity || lines <= 0)
            return layout;
        std::array<std::string_view, 8> wrapped{};
        int used = 0;
        while (!text.empty() && used < std::min<int>(lines, wrapped.size())) {
            size_t end = Utf8Text::prefixBytes(text, capacity);
            if (end < text.size() && used + 1 < lines) {
                const auto space = text.rfind(' ', end);
                if (space != std::string_view::npos && space > 0)
                    end = space;
            }
            wrapped[used++] = text.substr(0, end);
            text.remove_prefix(end);
            while (!text.empty() && text.front() == ' ')
                text.remove_prefix(1);
        }
        layout.lines.reserve(used);
        int16_t y = rect.y + (rect.h - used * height) / 2;
        for (int i = 0; i < used; ++i) {
            std::string clipped;
            auto line = wrapped[i];
            if (ellipsis && i + 1 == used && !text.empty()) {
                const size_t dots = std::min<size_t>(3, capacity);
                clipped =
                    std::string{line.substr(0, Utf8Text::prefixBytes(line, capacity - dots))} + std::string(dots, '.');
                line = clipped;
            }
            appendText(layout, {rect.x, y, rect.w, static_cast<int16_t>(height)}, line, textSize, align, 1);
            y += height;
        }
        layout.consumed = originalLength - text.size();
        return layout;
    }

} // namespace ui
