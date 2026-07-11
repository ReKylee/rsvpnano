#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "standby/PackedGrid.h"

namespace standby {

    struct LifePoint {
        int8_t x;
        int8_t y;
    };

    class IncrementalLifeGrid {
    public:
        void reset(uint16_t columns, uint16_t rows);
        void clear();
        void setAliveAt(int x, int y, bool alive);
        void stampPattern(const LifePoint* points, size_t pointCount, int originX, int originY);
        void finishSeed();

        size_t step();

        PackedGridView liveCells() const {
            return viewOf(liveCells_, wordCount_);
        }
        PackedGridView dirtyCells() const {
            return viewOf(dirtyCells_, wordCount_);
        }
        size_t liveCount() const {
            return liveCount_;
        }
        bool fullRedraw() const {
            return fullRedraw_;
        }
        void clearFullRedraw() {
            fullRedraw_ = false;
        }

    private:
        static constexpr uint8_t kAliveMask = 0x80;
        static constexpr uint8_t kNeighborMask = 0x0F;

        size_t index(uint16_t x, uint16_t y) const;
        size_t wrappedIndex(int x, int y) const;
        bool alive(size_t index) const;
        uint8_t neighborCount(size_t index) const;
        void setAliveBit(size_t index, bool isAlive);
        void addNeighborDelta(size_t index, int8_t delta);
        void markActive(size_t index);
        void markDirty(size_t index);
        void rebuildNeighborCounts();
        void rebuildLiveCells();

        uint16_t columns_ = 0;
        uint16_t rows_ = 0;
        size_t cellCount_ = 0;
        size_t wordCount_ = 0;
        size_t liveCount_ = 0;
        size_t changeCount_ = 0;
        bool fullRedraw_ = true;
        std::array<uint8_t, kMaxStandbyCells> cells_{}; // bit 7 = alive, bits 0..3 = exact neighbor count 0..8
        PackedGridStorage active_{}; // cells that can possibly change this generation
        PackedGridStorage nextActive_{}; // neighbors of cells that changed this generation
        PackedGridStorage dirtyCells_{}; // cells whose visual state changed this generation
        PackedGridStorage liveCells_{}; // packed 1-bit grid consumed by renderer
        std::array<uint16_t, kMaxStandbyCells> changes_{}; // changed cell indices for neighbor-count updates
    };

} // namespace standby
