#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace locales {

    using UiFont = std::vector<uint8_t>;

    inline uint8_t uiFontCellWidth(std::span<const uint8_t> font) {
        return font[9];
    }

    inline uint8_t uiFontHeight(std::span<const uint8_t> font) {
        return font[10];
    }

} // namespace locales
