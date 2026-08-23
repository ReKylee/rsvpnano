#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Utf8Text {

    constexpr bool isScalarValue(uint32_t codepoint) {
        return codepoint <= 0x10FFFFU && (codepoint < 0xD800U || codepoint > 0xDFFFU);
    }

    constexpr bool isContinuation(uint8_t value) {
        return (value & 0xC0U) == 0x80U;
    }

    inline bool decode(std::string_view& text, uint32_t& codepoint) {
        if (text.empty())
            return false;

        const uint8_t first = static_cast<uint8_t>(text.front());
        text.remove_prefix(1);
        if (first < 0x80U) {
            codepoint = first;
            return true;
        }

        uint8_t count = 0;
        uint32_t minimum = 0;
        if ((first & 0xE0U) == 0xC0U) {
            count = 1;
            minimum = 0x80U;
            codepoint = first & 0x1FU;
        } else if ((first & 0xF0U) == 0xE0U) {
            count = 2;
            minimum = 0x800U;
            codepoint = first & 0x0FU;
        } else if ((first & 0xF8U) == 0xF0U) {
            count = 3;
            minimum = 0x10000U;
            codepoint = first & 0x07U;
        } else {
            return false;
        }

        if (text.size() < count)
            return false;
        for (uint8_t index = 0; index < count; ++index) {
            const uint8_t value = static_cast<uint8_t>(text[index]);
            if (!isContinuation(value))
                return false;
            codepoint = (codepoint << 6U) | (value & 0x3FU);
        }
        if (codepoint < minimum || !isScalarValue(codepoint)) {
            return false;
        }

        text.remove_prefix(count);
        return true;
    }

    inline bool next(std::string_view& text, uint32_t& codepoint) {
        if (text.empty())
            return false;
        if (!decode(text, codepoint))
            codepoint = '?';
        return true;
    }

    inline bool next(std::string_view& text, uint16_t& codepoint) {
        uint32_t value = 0;
        if (!next(text, value))
            return false;
        codepoint = value <= UINT16_MAX ? static_cast<uint16_t>(value) : static_cast<uint16_t>('?');
        return true;
    }

    template<typename Output>
    inline bool append(Output& text, uint32_t codepoint) {
        if (!isScalarValue(codepoint))
            return false;
        if (codepoint <= 0x7FU) {
            text += static_cast<char>(codepoint);
        } else if (codepoint <= 0x7FFU) {
            text += static_cast<char>(0xC0U | (codepoint >> 6U));
            text += static_cast<char>(0x80U | (codepoint & 0x3FU));
        } else if (codepoint <= 0xFFFFU) {
            text += static_cast<char>(0xE0U | (codepoint >> 12U));
            text += static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
            text += static_cast<char>(0x80U | (codepoint & 0x3FU));
        } else {
            text += static_cast<char>(0xF0U | (codepoint >> 18U));
            text += static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU));
            text += static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
            text += static_cast<char>(0x80U | (codepoint & 0x3FU));
        }
        return true;
    }

    inline size_t count(std::string_view text) {
        size_t result = 0;
        uint32_t codepoint = 0;
        while (next(text, codepoint))
            ++result;
        return result;
    }

    inline size_t prefixBytes(std::string_view text, size_t codepoints) {
        const size_t originalSize = text.size();
        uint32_t codepoint = 0;
        while (codepoints > 0 && next(text, codepoint))
            --codepoints;
        return originalSize - text.size();
    }

    inline std::string_view suffix(std::string_view text, size_t maximumBytes) {
        if (text.size() <= maximumBytes)
            return text;
        text.remove_prefix(text.size() - maximumBytes);
        while (!text.empty() && isContinuation(static_cast<uint8_t>(text.front())))
            text.remove_prefix(1);
        return text;
    }

    inline size_t lastCodepointStart(std::string_view text) {
        if (text.empty())
            return 0;
        size_t start = text.size() - 1;
        while (start > 0 && isContinuation(static_cast<uint8_t>(text[start])))
            --start;
        return start;
    }

} // namespace Utf8Text
