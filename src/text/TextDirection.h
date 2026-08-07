#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

enum class TextDirection : uint8_t {
    automatic,
    ltr,
    rtl,
};

constexpr std::string_view toString(TextDirection direction) {
    switch (direction) {
    case TextDirection::ltr:
        return "ltr";
    case TextDirection::rtl:
        return "rtl";
    default:
        return "auto";
    }
}

constexpr std::optional<TextDirection> textDirection(std::string_view value) {
    if (value == "auto")
        return TextDirection::automatic;
    if (value == "ltr")
        return TextDirection::ltr;
    if (value == "rtl")
        return TextDirection::rtl;
    return std::nullopt;
}
