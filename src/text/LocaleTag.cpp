#include "text/LocaleTag.h"

#include <algorithm>
#include <cctype>
#include <ranges>

#include "text/AsciiText.h"

namespace LocaleTag {

    std::expected<std::string, std::string> normalize(std::string_view locale) {
        if (locale.empty() || locale.size() > 35 || locale.contains('_'))
            return std::unexpected("invalid BCP 47 locale");

        std::string normalized;
        normalized.reserve(locale.size());
        size_t index = 0;
        while (!locale.empty()) {
            const size_t separator = locale.find('-');
            const std::string_view part = locale.substr(0, separator);
            if (part.empty() || part.size() > 8 || !std::ranges::all_of(part, AsciiText::isAlphaNumeric))
                return std::unexpected("invalid BCP 47 locale");
            if (index == 0 && (part.size() < 2 || !std::ranges::all_of(part, AsciiText::isAlpha)))
                return std::unexpected("invalid BCP 47 language subtag");
            if (!normalized.empty())
                normalized.push_back('-');
            for (size_t character = 0; character < part.size(); ++character) {
                char value = part[character];
                if (index == 1 && part.size() == 4 && std::ranges::all_of(part, AsciiText::isAlpha))
                    value = character == 0 ? std::toupper(static_cast<unsigned char>(value))
                                           : AsciiText::toLower(value);
                else if (index > 0 && part.size() == 2 && std::ranges::all_of(part, AsciiText::isAlpha))
                    value = std::toupper(static_cast<unsigned char>(value));
                else
                    value = AsciiText::toLower(value);
                normalized.push_back(value);
            }
            ++index;
            if (separator == std::string_view::npos)
                break;
            locale.remove_prefix(separator + 1);
        }
        return normalized;
    }

} // namespace LocaleTag
