#include "standby/MazeScreensaver.h"

#include <algorithm>

namespace standby {

    namespace {
        constexpr uint8_t kCarvesPerFrame = 18;
        constexpr uint16_t kFinishedHoldFrames = 240;
        constexpr int8_t kNeighborDx[] = {1, -1, 0, 0};
        constexpr int8_t kNeighborDy[] = {0, 0, 1, -1};

    } // namespace

    void MazeScreensaver::reset(uint16_t columns, uint16_t rows) {
        columns_ = std::min<uint16_t>(columns, kMaxStandbyColumns);
        rows_ = std::min<uint16_t>(rows, kMaxStandbyRows);
        displayWordCount_ = packedWordCount(static_cast<size_t>(columns_) * rows_);
    }

    void MazeScreensaver::seed(uint32_t rngSeed) {
        rng_ = rngSeed == 0 ? 1U : rngSeed;
        generation_ = 0;
        settledFrames_ = 0;
        fullRedraw_ = true;
        stackSize_ = 0;

        clearPackedGrid(cells_, displayWordCount_);
        clearPackedGrid(dimCells_, displayWordCount_);
        clearPackedGrid(dirtyCells_, displayWordCount_);

        const uint16_t mazeCols = mazeColumns();
        const uint16_t mazeRowsValue = mazeRows();
        const uint16_t startX = static_cast<uint16_t>((advanceRng(rng_) >> 8) % mazeCols);
        const uint16_t startY = static_cast<uint16_t>((advanceRng(rng_) >> 8) % mazeRowsValue);
        const uint16_t start = static_cast<uint16_t>(startY * mazeCols + startX);

        push(start);
        carveMazeCell(start);
        updateHead({});
    }

    void MazeScreensaver::step() {
        fullRedraw_ = false;
        clearPackedGrid(dirtyCells_, displayWordCount_);

        const DisplayPoint previousHead = stackSize_ == 0 ? DisplayPoint{} : displayPoint(top());

        if (stackSize_ == 0) {
            updateHead(previousHead);
            if (++settledFrames_ >= kFinishedHoldFrames) {
                seed(advanceRng(rng_));
            }
            return;
        }

        settledFrames_ = 0;
        const uint16_t mazeCols = mazeColumns();
        const uint16_t mazeRowsValue = mazeRows();

        for (uint8_t stepIndex = 0; stepIndex < kCarvesPerFrame && stackSize_ > 0; ++stepIndex) {
            const uint16_t current = top();
            const uint16_t cx = static_cast<uint16_t>(current % mazeCols);
            const uint16_t cy = static_cast<uint16_t>(current / mazeCols);
            std::array<uint16_t, 4> candidates;
            uint8_t candidateCount = 0;

            for (uint8_t direction = 0; direction < 4; ++direction) {
                const int nx = static_cast<int>(cx) + kNeighborDx[direction];
                const int ny = static_cast<int>(cy) + kNeighborDy[direction];
                if (nx < 0 || ny < 0 || nx >= static_cast<int>(mazeCols)
                    || ny >= static_cast<int>(mazeRowsValue)) {
                    continue;
                }
                const uint16_t encoded = static_cast<uint16_t>(ny * mazeCols + nx);
                const size_t displayIndex = static_cast<size_t>(ny * 2 + 1) * columns_ + nx * 2 + 1;
                const uint32_t mask = 1UL << (displayIndex % kPackedBitsPerWord);
                if ((dimCells_[displayIndex / kPackedBitsPerWord] & mask) == 0) {
                    candidates[candidateCount++] = encoded;
                }
            }

            if (candidateCount == 0) {
                pop();
                continue;
            }

            const uint16_t next = candidates[(advanceRng(rng_) >> 16) % candidateCount];
            push(next);
            carveMazeCell(next);
            carveMazeWall(current, next);
        }

        updateHead(previousHead);
        ++generation_;
    }

    Frame MazeScreensaver::frame() const {
        return Frame{viewOf(cells_, displayWordCount_), viewOf(dimCells_, displayWordCount_),
                     viewOf(dirtyCells_, displayWordCount_), generation_, fullRedraw_};
    }

    uint16_t MazeScreensaver::mazeColumns() const {
        return std::max<uint16_t>(1, (columns_ - 1U) / 2U);
    }

    uint16_t MazeScreensaver::mazeRows() const {
        return std::max<uint16_t>(1, (rows_ - 1U) / 2U);
    }

    size_t MazeScreensaver::mazeCellCount() const {
        return static_cast<size_t>(mazeColumns()) * mazeRows();
    }

    void MazeScreensaver::push(uint16_t index) {
        if (stackSize_ < stack_.size()) {
            stack_[stackSize_++] = index;
        }
    }

    void MazeScreensaver::pop() {
        if (stackSize_ > 0) {
            --stackSize_;
        }
    }

    uint16_t MazeScreensaver::top() const {
        return stackSize_ == 0 ? 0 : stack_[stackSize_ - 1];
    }

    MazeScreensaver::DisplayPoint MazeScreensaver::displayPoint(uint16_t mazeIndex) const {
        if (mazeIndex >= mazeCellCount()) {
            return {};
        }
        const uint16_t mazeCols = mazeColumns();
        const uint16_t mazeX = static_cast<uint16_t>(mazeIndex % mazeCols);
        const uint16_t mazeY = static_cast<uint16_t>(mazeIndex / mazeCols);
        return {static_cast<int16_t>(mazeX * 2 + 1), static_cast<int16_t>(mazeY * 2 + 1), true};
    }

    void MazeScreensaver::setDisplayCell(PackedGridStorage& cells, int16_t x, int16_t y, bool alive) {
        if (x < 0 || y < 0 || x >= static_cast<int16_t>(columns_) || y >= static_cast<int16_t>(rows_)) {
            return;
        }

        const size_t index = static_cast<size_t>(y) * columns_ + static_cast<uint16_t>(x);
        const size_t word = index / kPackedBitsPerWord;
        const uint32_t mask = 1UL << (index % kPackedBitsPerWord);
        if (((cells[word] & mask) != 0) == alive) {
            return;
        }

        if (alive)
            cells[word] |= mask;
        else
            cells[word] &= ~mask;
        markDirty(x, y);
    }

    void MazeScreensaver::setDisplayCell(PackedGridStorage& cells, DisplayPoint point, bool alive) {
        if (point.valid) {
            setDisplayCell(cells, point.x, point.y, alive);
        }
    }

    void MazeScreensaver::markDirty(int16_t x, int16_t y) {
        if (x < 0 || y < 0 || x >= static_cast<int16_t>(columns_) || y >= static_cast<int16_t>(rows_)) {
            return;
        }
        const size_t index = static_cast<size_t>(y) * columns_ + static_cast<uint16_t>(x);
        dirtyCells_[index / kPackedBitsPerWord] |= 1UL << (index % kPackedBitsPerWord);
    }

    void MazeScreensaver::markDirty(DisplayPoint point) {
        if (point.valid) {
            markDirty(point.x, point.y);
        }
    }

    void MazeScreensaver::carveMazeCell(uint16_t mazeIndex) {
        setDisplayCell(dimCells_, displayPoint(mazeIndex), true);
    }

    void MazeScreensaver::carveMazeWall(uint16_t from, uint16_t to) {
        const DisplayPoint a = displayPoint(from);
        const DisplayPoint b = displayPoint(to);
        if (!a.valid || !b.valid) {
            return;
        }
        setDisplayCell(dimCells_, static_cast<int16_t>((a.x + b.x) / 2), static_cast<int16_t>((a.y + b.y) / 2), true);
    }

    void MazeScreensaver::updateHead(DisplayPoint previousHead) {
        setDisplayCell(cells_, previousHead, false);
        if (stackSize_ > 0) {
            setDisplayCell(cells_, displayPoint(top()), true);
        }
    }

} // namespace standby
