#pragma once

#include <cstdint>

#include "standby/PackedGrid.h"

namespace standby {

    enum class Kind : uint8_t {
        Life = 0,
        Maze,
        Voronoi,
        ScreenOff,
    };

    struct Frame {
        PackedGridView cells{}; // bright/live cells, packed bits
        PackedGridView dimCells{}; // optional dim layer, packed bits
        PackedGridView dirtyCells{}; // cells that changed; invalid means redraw all
        uint32_t generation = 0;
        bool fullRedraw = true;
    };

} // namespace standby
