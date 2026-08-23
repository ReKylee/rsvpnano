#include "text/RsvpDirectives.h"

#include <algorithm>

#include "board/BoardStorage.h"

#include "storage/fs/StoragePaths.h"
#include "text/AsciiText.h"
#include "text/TextNormalizer.h"

namespace RsvpText {

    using namespace StoragePaths;

    constexpr size_t kMaxChapterTitleChars = 64;

    bool prefixHasBoundary(std::string_view text, std::string_view prefix) {
        const size_t prefixLength = prefix.length();
        if (text.length() < prefixLength
            || !std::ranges::equal(text.substr(0, prefixLength), prefix, {}, AsciiText::toLower, AsciiText::toLower))
            return false;
        if (text.length() == prefixLength) {
            return true;
        }

        const char next = text[prefixLength];
        return AsciiText::isWhitespace(next) || next == ':' || next == '.' || next == '-';
    }

    std::string_view stripBom(std::string_view text) {
        text = AsciiText::trim(text);
        if (text.length() >= 3 && static_cast<uint8_t>(text[0]) == 0xEF && static_cast<uint8_t>(text[1]) == 0xBB
            && static_cast<uint8_t>(text[2]) == 0xBF) {
            text.remove_prefix(3);
            text = AsciiText::trim(text);
        }
        return text;
    }

    bool chapterTitleFromLine(std::string_view line, std::string& title) {
        const std::string trimmed = normalizeDisplayText(stripBom(line));
        if (trimmed.empty() || trimmed.length() > kMaxChapterTitleChars) {
            return false;
        }

        if (trimmed.starts_with('#')) {
            size_t prefixLength = 0;
            while (prefixLength < trimmed.length() && trimmed[prefixLength] == '#') {
                ++prefixLength;
            }
            title = AsciiText::trim(std::string_view{trimmed}.substr(prefixLength));
            return !title.empty();
        }

        if (prefixHasBoundary(trimmed, "chapter") || prefixHasBoundary(trimmed, "part")
            || prefixHasBoundary(trimmed, "book")) {
            title = trimmed;
            return true;
        }

        return false;
    }

    std::string directiveValue(std::string_view line, std::string_view directive) {
        std::string_view value = AsciiText::trim(line.substr(directive.length()));
        if (!value.empty() && (value.front() == ':' || value.front() == '-' || value.front() == '.')) {
            value.remove_prefix(1);
            value = AsciiText::trim(value);
        }
        return normalizeDisplayText(value);
    }

    RsvpDirectiveValues readRsvpDirectiveValues(std::string_view path) {
        RsvpDirectiveValues values;
        const std::string ownedPath{path};
        if (!hasRsvpExtension(path)) {
            return values;
        }

        File file = Board::Storage::filesystem().open(ownedPath.c_str());
        if (!file || file.isDirectory()) {
            if (file) {
                file.close();
            }
            return values;
        }

        std::string line;
        line.reserve(kMaxChapterTitleChars + 16);
        while (file.available()) {
            const char c = static_cast<char>(file.read());
            if (c == '\r') {
                continue;
            }

            if (c != '\n') {
                line += c;
                if (line.length() > kMaxChapterTitleChars + 16) {
                    line.clear();
                    break;
                }
                continue;
            }

            const std::string_view trimmed = stripBom(line);
            if (trimmed.empty()) {
                line.clear();
                continue;
            }

            if (values.title.empty() && prefixHasBoundary(trimmed, "@title")) {
                values.title = directiveValue(trimmed, "@title");
            } else if (values.author.empty() && prefixHasBoundary(trimmed, "@author")) {
                values.author = directiveValue(trimmed, "@author");
            } else if (!trimmed.starts_with('@')) {
                break;
            }

            if (!values.title.empty() && !values.author.empty()) {
                break;
            }
            line.clear();
        }

        file.close();
        return values;
    }

} // namespace RsvpText
