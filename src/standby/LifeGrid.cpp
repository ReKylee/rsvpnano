#include "standby/LifeGrid.h"

#include <algorithm>

namespace standby {

    void IncrementalLifeGrid::reset(uint16_t columns, uint16_t rows) {
        columns_ = std::min<uint16_t>(columns, kMaxStandbyColumns);
        rows_ = std::min<uint16_t>(rows, kMaxStandbyRows);
        cellCount_ = static_cast<size_t>(columns_) * rows_;
        wordCount_ = packedWordCount(cellCount_);
        clear();
    }

    void IncrementalLifeGrid::clear() {
        std::fill_n(cells_.begin(), cellCount_, uint8_t{0});
        clearPackedGrid(active_, wordCount_);
        clearPackedGrid(nextActive_, wordCount_);
        clearPackedGrid(dirtyCells_, wordCount_);
        clearPackedGrid(liveCells_, wordCount_);
        liveCount_ = 0;
        changeCount_ = 0;
        fullRedraw_ = true;
    }

    void IncrementalLifeGrid::setAliveAt(int x, int y, bool isAlive) {
        if (columns_ == 0 || rows_ == 0) {
            return;
        }
        const size_t cellIndex = wrappedIndex(x, y);
        if (alive(cellIndex) == isAlive) {
            return;
        }
        setAliveBit(cellIndex, isAlive);
        markActive(cellIndex);
    }

    void IncrementalLifeGrid::stampPattern(const LifePoint* points, size_t pointCount, int originX, int originY) {
        if (points == nullptr) {
            return;
        }
        for (size_t i = 0; i < pointCount; ++i) {
            setAliveAt(originX + points[i].x, originY + points[i].y, true);
        }
    }

    void IncrementalLifeGrid::finishSeed() {
        rebuildNeighborCounts();
        rebuildLiveCells();
        clearPackedGrid(dirtyCells_, wordCount_);
        std::fill_n(active_.begin(), wordCount_, 0xFFFFFFFFUL);
        fullRedraw_ = true;
    }

    size_t IncrementalLifeGrid::step() {
        if (cellCount_ == 0) {
            return 0;
        }

        clearPackedGrid(dirtyCells_, wordCount_);
        clearPackedGrid(nextActive_, wordCount_);
        changeCount_ = 0;

        for (size_t cellIndex = 0; cellIndex < cellCount_; ++cellIndex) {
            if (!cellAlive(viewOf(active_, wordCount_), cellIndex)) {
                continue;
            }
            const bool wasAlive = alive(cellIndex);
            const uint8_t neighbors = neighborCount(cellIndex);
            const bool shouldLive = neighbors == 3 || (wasAlive && neighbors == 2);
            if (shouldLive == wasAlive) {
                continue;
            }

            setAliveBit(cellIndex, shouldLive);
            markDirty(cellIndex);
            if (changeCount_ < changes_.size()) {
                changes_[changeCount_++] = static_cast<uint16_t>(cellIndex);
            }
        }

        for (size_t i = 0; i < changeCount_; ++i) {
            const size_t changed = changes_[i];
            const int x = static_cast<int>(changed % columns_);
            const int y = static_cast<int>(changed / columns_);
            const int8_t delta = alive(changed) ? 1 : -1;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const size_t affected = wrappedIndex(x + dx, y + dy);
                    markActive(affected);
                    if (dx != 0 || dy != 0) {
                        addNeighborDelta(affected, delta);
                    }
                }
            }
        }

        active_ = nextActive_;
        rebuildLiveCells();
        return liveCount_;
    }

    size_t IncrementalLifeGrid::index(uint16_t x, uint16_t y) const {
        return static_cast<size_t>(y) * columns_ + x;
    }

    size_t IncrementalLifeGrid::wrappedIndex(int x, int y) const {
        if (columns_ == 0 || rows_ == 0) {
            return 0;
        }
        while (x < 0) {
            x += columns_;
        }
        while (y < 0) {
            y += rows_;
        }
        x %= columns_;
        y %= rows_;
        return index(static_cast<uint16_t>(x), static_cast<uint16_t>(y));
    }

    bool IncrementalLifeGrid::alive(size_t cellIndex) const {
        return cellIndex < cellCount_ && (cells_[cellIndex] & kAliveMask) != 0;
    }

    uint8_t IncrementalLifeGrid::neighborCount(size_t cellIndex) const {
        return cellIndex < cellCount_ ? static_cast<uint8_t>(cells_[cellIndex] & kNeighborMask) : 0;
    }

    void IncrementalLifeGrid::setAliveBit(size_t cellIndex, bool isAlive) {
        if (cellIndex >= cellCount_) {
            return;
        }
        const bool wasAlive = alive(cellIndex);
        if (wasAlive == isAlive) {
            return;
        }
        if (isAlive) {
            cells_[cellIndex] |= kAliveMask;
            ++liveCount_;
        } else {
            cells_[cellIndex] &= static_cast<uint8_t>(~kAliveMask);
            if (liveCount_ > 0) {
                --liveCount_;
            }
        }
    }

    void IncrementalLifeGrid::addNeighborDelta(size_t cellIndex, int8_t delta) {
        if (cellIndex >= cellCount_) {
            return;
        }
        const int value = static_cast<int>(neighborCount(cellIndex)) + delta;
        const uint8_t clamped = static_cast<uint8_t>(std::clamp(value, 0, 8));
        cells_[cellIndex] = static_cast<uint8_t>((cells_[cellIndex] & kAliveMask) | clamped);
    }

    void IncrementalLifeGrid::markActive(size_t cellIndex) {
        if (cellIndex < cellCount_) {
            setCell(nextActive_, cellIndex, true);
        }
    }

    void IncrementalLifeGrid::markDirty(size_t cellIndex) {
        if (cellIndex < cellCount_) {
            setCell(dirtyCells_, cellIndex, true);
        }
    }

    void IncrementalLifeGrid::rebuildNeighborCounts() {
        for (size_t i = 0; i < cellCount_; ++i) {
            cells_[i] &= kAliveMask;
        }
        for (uint16_t y = 0; y < rows_; ++y) {
            for (uint16_t x = 0; x < columns_; ++x) {
                const size_t cellIndex = index(x, y);
                if (!alive(cellIndex)) {
                    continue;
                }
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) {
                            continue;
                        }
                        addNeighborDelta(wrappedIndex(static_cast<int>(x) + dx, static_cast<int>(y) + dy), 1);
                    }
                }
            }
        }
    }

    void IncrementalLifeGrid::rebuildLiveCells() {
        clearPackedGrid(liveCells_, wordCount_);
        for (size_t i = 0; i < cellCount_; ++i) {
            if (alive(i)) {
                setCell(liveCells_, i, true);
            }
        }
    }

} // namespace standby
