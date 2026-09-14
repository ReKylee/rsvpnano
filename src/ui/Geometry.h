#pragma once

#include <algorithm>
#include <cstdint>

namespace ui {

    struct Rect {
        int16_t x = 0;
        int16_t y = 0;
        int16_t w = 0;
        int16_t h = 0;
    };

    constexpr bool operator==(Rect left, Rect right) {
        return left.x == right.x && left.y == right.y && left.w == right.w && left.h == right.h;
    }

    constexpr bool contains(Rect rect, uint16_t x, uint16_t y) {
        return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
    }

    constexpr Rect intersection(Rect left, Rect right) {
        const int16_t x = std::max(left.x, right.x);
        const int16_t y = std::max(left.y, right.y);
        const int x2 = std::min<int>(left.x + left.w, right.x + right.w);
        const int y2 = std::min<int>(left.y + left.h, right.y + right.h);
        return {x, y, static_cast<int16_t>(std::max(0, x2 - x)), static_cast<int16_t>(std::max(0, y2 - y))};
    }

    constexpr Rect rotateClockwise(Rect rect, int16_t sourceWidth) {
        return {rect.y, static_cast<int16_t>(sourceWidth - rect.x - rect.w), rect.h, rect.w};
    }

    struct Column {
        Rect bounds;
        int16_t gap = 0;
        int16_t cursor = 0;

        constexpr Rect next(int16_t height) {
            const Rect result{bounds.x, static_cast<int16_t>(bounds.y + cursor), bounds.w, height};
            cursor = static_cast<int16_t>(cursor + height + gap);
            return result;
        }
    };

    struct Row {
        Rect bounds;
        int16_t gap = 0;
        int16_t cursor = 0;

        constexpr Rect next(int16_t width) {
            const Rect result{static_cast<int16_t>(bounds.x + cursor), bounds.y, width, bounds.h};
            cursor = static_cast<int16_t>(cursor + width + gap);
            return result;
        }
    };

    struct Grid {
        Rect bounds;
        uint8_t columns = 1;
        int16_t rowHeight = 0;
        int16_t gap = 0;
        uint16_t index = 0;

        constexpr Rect next() {
            const uint8_t safeColumns = columns == 0 ? 1 : columns;
            const int16_t cellWidth = static_cast<int16_t>((bounds.w - gap * (safeColumns - 1)) / safeColumns);
            const uint16_t column = index % safeColumns;
            const uint16_t row = index / safeColumns;
            ++index;
            return {static_cast<int16_t>(bounds.x + column * (cellWidth + gap)),
                    static_cast<int16_t>(bounds.y + row * (rowHeight + gap)), cellWidth, rowHeight};
        }
    };

} // namespace ui
