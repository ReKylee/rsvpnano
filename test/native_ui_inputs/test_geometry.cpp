#include "ui/Geometry.h"
#include <array>
#include <cassert>
#include <iostream>

constexpr std::array<uint8_t, 3> spans{1, ui::Grid::FullRow, 1};
static_assert(ui::gridRows(spans, 2) == 3);
static_assert(ui::gridRows(spans, 1) == 3);
static_assert(ui::gridRows(spans, 0) == 3);
static_assert(ui::gridRows(std::array<uint8_t, 0>{}, 3) == 0);

constexpr bool consecutiveFullRows() {
    ui::Grid grid{{4, 8, 203, 180}, 3, 30, 4};
    if (grid.rowsUsed() != 0 || grid.next() != ui::Rect{4, 8, 65, 30})
        return false;
    if (grid.next(ui::Grid::FullRow) != ui::Rect{4, 42, 203, 30} || grid.rowsUsed() != 2)
        return false;
    if (grid.next(ui::Grid::FullRow) != ui::Rect{4, 76, 203, 30} || grid.rowsUsed() != 3)
        return false;
    if (grid.next(2) != ui::Rect{4, 110, 134, 30} || grid.rowsUsed() != 4)
        return false;
    if (grid.next() != ui::Rect{142, 110, 65, 30} || grid.rowsUsed() != 4)
        return false;
    return grid.next(ui::Grid::FullRow) == ui::Rect{4, 144, 203, 30} && grid.rowsUsed() == 5;
}
static_assert(consecutiveFullRows());

constexpr bool normalizedSingleColumn() {
    for (const uint8_t columns : {0, 1}) {
        ui::Grid grid{{7, 9, 120, 100}, columns, 20, 2};
        if (grid.next(0) != ui::Rect{7, 9, 120, 20} || grid.rowsUsed() != 1)
            return false;
        if (grid.next(ui::Grid::FullRow) != ui::Rect{7, 31, 120, 20} || grid.rowsUsed() != 2)
            return false;
        if (grid.next() != ui::Rect{7, 53, 120, 20} || grid.rowsUsed() != 3)
            return false;
    }
    return true;
}
static_assert(normalizedSingleColumn());

void testRectangleOperations() {
    const ui::Rect bounds{10, 20, 30, 40};
    assert(ui::contains(bounds, 10, 20) && ui::contains(bounds, 39, 59));
    assert(!ui::contains(bounds, 40, 59) && !ui::contains(bounds, 39, 60));
    assert(!ui::contains({}, 0, 0) && !ui::contains({0, 0, -1, 10}, 0, 0));
    assert(ui::contains({-10, -10, 20, 20}, 0, 0));
    assert((ui::intersection(bounds, {30, 50, 40, 40}) == ui::Rect{30, 50, 10, 10}));
    assert((ui::intersection(bounds, {40, 60, 10, 10}) == ui::Rect{40, 60, 0, 0}));
    assert((ui::rotateClockwise(bounds, 100) == ui::Rect{20, 60, 40, 30}));
    ui::Row row{bounds, 2};
    assert((row.next(8) == ui::Rect{10, 20, 8, 40}));
    assert((row.next(12) == ui::Rect{20, 20, 12, 40}));
    ui::Column column{bounds, 3};
    assert((column.next(7) == ui::Rect{10, 20, 30, 7}));
    assert((column.next(11) == ui::Rect{10, 30, 30, 11}));
}

void testIndexExhaustion() {
    for (const uint8_t columns : {0, 1, 2, 3, 255}) {
        ui::Grid grid{{4, 8, 301, 180}, columns, 0, 0, UINT16_MAX};
        const auto rows = grid.rowsUsed();
        assert(grid.next() == ui::Rect{});
        assert(grid.next(ui::Grid::FullRow) == ui::Rect{});
        assert(grid.index == UINT16_MAX && grid.rowsUsed() == rows);
    }
    // A failed spanning request must not consume the two remaining cells.
    ui::Grid partial{{4, 8, 301, 180}, 3, 0, 0, UINT16_MAX - 2};
    const auto before = partial.index;
    assert(partial.next(ui::Grid::FullRow) == ui::Rect{} && partial.index == before);
    assert(partial.next().w > 0 && partial.index == UINT16_MAX - 1);
    assert(partial.next().w > 0 && partial.index == UINT16_MAX);
    assert(partial.next() == ui::Rect{} && partial.index == UINT16_MAX);
}

int main() {
    assert(consecutiveFullRows() && normalizedSingleColumn());
    testRectangleOperations();
    testIndexExhaustion();
    ui::Grid grid{{4, 8, 201, 180}, 2, 30, 4};
    assert((grid.next() == ui::Rect{4, 8, 98, 30}));
    assert((grid.next(ui::Grid::FullRow) == ui::Rect{4, 42, 201, 30}));
    assert((grid.next() == ui::Rect{4, 76, 98, 30}));
    assert(grid.rowsUsed() == 3);

    struct Tile { uint8_t span; bool visible; };
    std::array tiles{Tile{2, true}, Tile{1, false}, Tile{2, true}};
    auto visible = tiles | std::views::filter(&Tile::visible);
    assert(ui::gridRows(visible, 3, &Tile::span) == 2);
    ui::Grid three{{10, 20, 308, 100}, 3, 24, 4};
    assert((three.next(2) == ui::Rect{10, 20, 204, 24}));
    assert((three.next(2) == ui::Rect{10, 48, 204, 24}));

    // Existing single-cell calls retain their positions, including zero-column normalization.
    for (uint8_t columns = 0; columns <= 4; ++columns) {
        ui::Grid cells{{3, 5, 301, 240}, columns, 21, 4};
        const int n = columns ? columns : 1;
        const int w = (301 - 4 * (n - 1)) / n;
        for (int index = 0; index < 64; ++index) {
            const auto item = cells.next();
            assert(item.x == 3 + (index % n) * (w + 4));
            assert(item.y == 5 + (index / n) * 25 && item.w == w && item.h == 21);
        }
    }
    std::cout << "Geometry: packing, rectangles, row/column operations and exhaustion passed\n";
}
