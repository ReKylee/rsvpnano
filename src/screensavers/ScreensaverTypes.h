#pragma once

#include <cstdint>

#include "screensavers/PackedGrid.h"

namespace standby {

    enum class Kind : uint8_t {
        life = 0,
        maze = 1,
        voronoi = 2,
        screenOff = 3,
        reaction = 4,
        Count,
    };

    struct Frame {
        PackedGridView cells{}; // bright/live cells, packed bits
        PackedGridView dimCells{}; // optional dim layer, packed bits
        PackedGridView dirtyCells{}; // cells that changed; invalid means redraw all
        uint32_t generation = 0;
        bool fullRedraw = true;
    };

} // namespace standby
