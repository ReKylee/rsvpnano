#include "ui/Geometry.h"
#include <array>
#include <cassert>
#include <iostream>

constexpr std::array<uint8_t, 3> spans{1, ui::Grid::FullRow, 1};
static_assert(ui::gridRows(spans, 2) == 3);
static_assert(ui::gridRows(spans, 1) == 3);
static_assert(ui::gridRows(spans, 0) == 3);
static_assert(ui::gridRows(std::array<uint8_t, 0>{}, 3) == 0);

int main() {
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
    ui::Grid exhausted{.bounds = {}, .columns = 3, .index = UINT16_MAX};
    assert(exhausted.next().w == 0 && exhausted.index == UINT16_MAX);
    std::cout << "Geometry: packing, measurement, spans and single-cell compatibility passed\n";
}
