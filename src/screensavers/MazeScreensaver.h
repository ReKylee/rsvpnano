#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "screensavers/ScreensaverTypes.h"

namespace standby {

    class MazeScreensaver final {
    public:
        void reset(uint16_t columns, uint16_t rows);
        void seed(uint32_t rngSeed);
        void step();
        Frame frame() const;

    private:
        static constexpr uint16_t kMaxMazeColumns = (kMaxStandbyColumns - 1U) / 2U;
        static constexpr uint16_t kMaxMazeRows = (kMaxStandbyRows - 1U) / 2U;
        static constexpr size_t kMaxMazeCells = static_cast<size_t>(kMaxMazeColumns) * kMaxMazeRows;

        struct DisplayPoint {
            int16_t x = 0;
            int16_t y = 0;
            bool valid = false;
        };

        uint16_t mazeColumns() const;
        uint16_t mazeRows() const;
        size_t mazeCellCount() const;
        void push(uint16_t index);
        void pop();
        uint16_t top() const;
        DisplayPoint displayPoint(uint16_t mazeIndex) const;
        void setDisplayCell(PackedGridStorage& cells, int16_t x, int16_t y, bool alive);
        void setDisplayCell(PackedGridStorage& cells, DisplayPoint point, bool alive);
        void markDirty(int16_t x, int16_t y);
        void markDirty(DisplayPoint point);
        void carveMazeCell(uint16_t mazeIndex);
        void carveMazeWall(uint16_t from, uint16_t to);
        void updateHead(DisplayPoint previousHead);

        uint16_t columns_ = 0;
        uint16_t rows_ = 0;
        uint32_t rng_ = 1;
        uint32_t generation_ = 0;
        uint16_t settledFrames_ = 0;
        bool fullRedraw_ = true;
        size_t displayWordCount_ = 0;
        uint16_t stackSize_ = 0;
        PackedGridStorage cells_{}; // bright current DFS head
        PackedGridStorage dimCells_{}; // carved maze corridors
        PackedGridStorage dirtyCells_{}; // changed display cells since last frame
        std::array<uint16_t, kMaxMazeCells> stack_{};
    };

} // namespace standby
