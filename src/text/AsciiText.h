#pragma once

#include <charconv>
#include <concepts>
#include <expected>
#include <string_view>
#include <system_error>

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

    constexpr int hexDigit(char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    template<std::unsigned_integral T>
    inline std::expected<T, std::error_code> parseUnsigned(std::string_view text, int base = 10) {
        T parsed = 0;
        const auto [end, error] = std::from_chars(text.begin(), text.end(), parsed, base);
        if (error != std::errc{})
            return std::unexpected(std::make_error_code(error));
        if (end != text.end())
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        return parsed;
    }

    constexpr char toLower(char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
    }

    constexpr std::string_view trim(std::string_view text) {
        while (!text.empty() && isWhitespace(text.front()))
            text.remove_prefix(1);
        while (!text.empty() && isWhitespace(text.back()))
            text.remove_suffix(1);
        return text;
    }

} // namespace AsciiText
