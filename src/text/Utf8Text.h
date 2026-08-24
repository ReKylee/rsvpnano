#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <SheenBidi/SBCodepoint.h>

namespace Utf8Text {

    constexpr bool isScalarValue(uint32_t codepoint) {
        return SBCodepointIsValid(codepoint);
    }

    constexpr bool isContinuation(uint8_t value) {
        return (value & 0xC0U) == 0x80U;
    }

    inline bool decode(std::string_view& text, uint32_t& codepoint) {
        if (text.empty())
            return false;
        SBUInteger bytes = 0;
        codepoint = SBCodepointDecodeNextFromUTF8(reinterpret_cast<const SBUInt8*>(text.data()), text.size(), &bytes);
        text.remove_prefix(codepoint == SBCodepointFaulty ? 1 : bytes);
        return codepoint != SBCodepointFaulty;
    }

    inline bool next(std::string_view& text, uint32_t& codepoint) {
        if (text.empty())
            return false;
        if (!decode(text, codepoint))
            codepoint = '?';
        return true;
    }

    inline size_t encode(uint32_t codepoint, std::span<char, 4> output) {
        if (!isScalarValue(codepoint))
            return 0;
        if (codepoint <= 0x7FU) {
            output[0] = static_cast<char>(codepoint);
            return 1;
        } else if (codepoint <= 0x7FFU) {
            output[0] = static_cast<char>(0xC0U | (codepoint >> 6U));
            output[1] = static_cast<char>(0x80U | (codepoint & 0x3FU));
            return 2;
        } else if (codepoint <= 0xFFFFU) {
            output[0] = static_cast<char>(0xE0U | (codepoint >> 12U));
            output[1] = static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
            output[2] = static_cast<char>(0x80U | (codepoint & 0x3FU));
            return 3;
        }
        output[0] = static_cast<char>(0xF0U | (codepoint >> 18U));
        output[1] = static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU));
        output[2] = static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
        output[3] = static_cast<char>(0x80U | (codepoint & 0x3FU));
        return 4;
    }

    template<typename Output>
    inline bool append(Output& text, uint32_t codepoint) {
        std::array<char, 4> encoded{};
        const size_t size = encode(codepoint, encoded);
        for (size_t index = 0; index < size; ++index)
            text += encoded[index];
        if (size == 0)
            return false;
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

    inline std::string_view prefix(std::string_view text, size_t maximumBytes) {
        if (text.size() <= maximumBytes)
            return text;
        size_t length = maximumBytes;
        while (length > 0 && isContinuation(static_cast<uint8_t>(text[length])))
            --length;
        return text.substr(0, length);
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
