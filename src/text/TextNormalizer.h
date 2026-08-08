#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace RsvpText {

    struct NormalizationStats {
        size_t malformedUtf8 = 0;
        size_t nonAsciiCodepoints = 0;
    };

    std::string normalizeDisplayText(std::string_view text, NormalizationStats* stats = nullptr);
    std::string uiSafeMetadata(std::string_view text);
    bool decodeMarkupEntity(std::string_view entity, std::string& decoded);
    std::string decodeMarkupEntities(std::string_view text);
    std::string readableKey(std::string_view text);

} // namespace RsvpText
