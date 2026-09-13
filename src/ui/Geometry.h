#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <ranges>

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
        static constexpr uint8_t FullRow = UINT8_MAX;

        Rect bounds;
        uint8_t columns = 1;
        int16_t rowHeight = 0;
        int16_t gap = 0;
        uint16_t index = 0;

        constexpr Rect next(uint8_t span = 1) {
            const uint8_t count = std::max<uint8_t>(1, columns);
            span = std::clamp<uint8_t>(span, 1, count);
            uint32_t cell = index;
            uint16_t column = cell % count;
            if (column + span > count) {
                cell += count - column;
                column = 0;
            }
            if (cell + span > UINT16_MAX)
                return {};
            index = static_cast<uint16_t>(cell + span);
            const int16_t cellWidth = static_cast<int16_t>((bounds.w - gap * (count - 1)) / count);
            const int16_t width = span == count
                                    ? bounds.w
                                    : static_cast<int16_t>(cellWidth * span + gap * (span - 1));
            return {static_cast<int16_t>(bounds.x + column * (cellWidth + gap)),
                    static_cast<int16_t>(bounds.y + (cell / count) * (rowHeight + gap)), width, rowHeight};
        }

        constexpr uint16_t rowsUsed() const {
            const uint8_t count = std::max<uint8_t>(1, columns);
            return static_cast<uint16_t>((static_cast<uint32_t>(index) + count - 1) / count);
        }
    };

    // Use the same packing rule for measurement and placement, including full-row entries.
    template<std::ranges::input_range Items, typename SpanFor = std::identity>
    constexpr uint16_t gridRows(Items&& items, uint8_t columns, SpanFor spanFor = {}) {
        Grid grid{.bounds = {}, .columns = columns};
        for (auto&& item : items)
            grid.next(std::invoke(spanFor, item));
        return grid.rowsUsed();
    }

} // namespace ui
