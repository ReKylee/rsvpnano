#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

enum class WritingMode : uint8_t {
    horizontalTb,
    verticalRl,
};

constexpr std::optional<WritingMode> writingMode(std::string_view value) {
    if (value == "horizontal-tb")
        return WritingMode::horizontalTb;
    if (value == "vertical-rl")
        return WritingMode::verticalRl;
    return std::nullopt;
}
