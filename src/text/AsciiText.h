#pragma once

#include <charconv>
#include <concepts>
#include <string_view>

namespace AsciiText {

    constexpr bool isWhitespace(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    }

    constexpr bool isAlpha(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }

    constexpr bool isDigit(char c) {
        return c >= '0' && c <= '9';
    }

    constexpr bool isAlphaNumeric(char c) {
        return isAlpha(c) || isDigit(c);
    }

    template<std::unsigned_integral T>
    inline bool parseUnsigned(std::string_view text, T& value, int base = 10) {
        T parsed = 0;
        const auto [end, error] = std::from_chars(text.begin(), text.end(), parsed, base);
        if (error != std::errc{} || end != text.end())
            return false;
        value = parsed;
        return true;
    }

    constexpr char toLower(char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
    }

} // namespace AsciiText
