#include "screensavers/ReactionScreensaver.h"

#include <algorithm>

namespace standby {

    void ReactionScreensaver::reset(uint16_t columns, uint16_t rows) {
        columns_ = std::min<uint16_t>(columns, kMaxStandbyColumns);
        rows_ = std::min<uint16_t>(rows, kMaxStandbyRows);
        cellCount_ = static_cast<size_t>(columns_) * rows_;
        wordCount_ = packedWordCount(cellCount_);
    }

    void ReactionScreensaver::seed(uint32_t rngSeed) {
        rng_ = rngSeed == 0 ? 1U : rngSeed;
        generation_ = 0;
        fullRedraw_ = true;
        for (size_t i = 0; i < cellCount_; ++i)
            states_[i] = static_cast<uint8_t>((advanceRng(rng_) >> 24U) % kStateCount);
        clearPackedGrid(cells_, wordCount_);
        clearPackedGrid(dimCells_, wordCount_);
        render();
        clearPackedGrid(dirtyCells_, wordCount_);
    }

    void ReactionScreensaver::step() {
        if (cellCount_ == 0)
            return;

        size_t changedCount = 0;
        for (int y = 0; y < static_cast<int>(rows_); ++y) {
            for (int x = 0; x < static_cast<int>(columns_); ++x) {
                const size_t cellIndex = static_cast<size_t>(y) * columns_ + x;
                const uint8_t state = states_[cellIndex];
                const uint8_t nextState = static_cast<uint8_t>((state + 1U) % kStateCount);
                bool advance = false;
                for (int dy = -1; dy <= 1 && !advance; ++dy) {
                    const int neighborY = (y + dy + rows_) % rows_;
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0)
                            continue;
                        const int neighborX = (x + dx + columns_) % columns_;
                        const size_t neighbor = static_cast<size_t>(neighborY) * columns_ + neighborX;
                        if (states_[neighbor] == nextState) {
                            advance = true;
                            break;
                        }
                    }
                }
                nextStates_[cellIndex] = advance ? nextState : state;
                changedCount += advance;
            }
        }

        states_.swap(nextStates_);
        ++generation_;
        fullRedraw_ = false;
        render();
        if (changedCount == 0 || generation_ > 2400)
            seed(advanceRng(rng_));
    }

    Frame ReactionScreensaver::frame() const {
        return Frame{viewOf(cells_, wordCount_), viewOf(dimCells_, wordCount_), viewOf(dirtyCells_, wordCount_),
                     generation_, fullRedraw_};
    }

    void ReactionScreensaver::render() {
        uint32_t brightWord = 0;
        uint32_t dimWord = 0;
        for (size_t cellIndex = 0; cellIndex < cellCount_; ++cellIndex) {
            const uint32_t mask = 1UL << (cellIndex % kPackedBitsPerWord);
            const uint8_t state = states_[cellIndex];
            if (state == 1)
                brightWord |= mask;
            else if (state >= 2 && state <= kDimStateEnd)
                dimWord |= mask;

            if (cellIndex % kPackedBitsPerWord == kPackedBitsPerWord - 1U || cellIndex + 1U == cellCount_) {
                const size_t word = cellIndex / kPackedBitsPerWord;
                dirtyCells_[word] = (cells_[word] ^ brightWord) | (dimCells_[word] ^ dimWord);
                cells_[word] = brightWord;
                dimCells_[word] = dimWord;
                brightWord = 0;
                dimWord = 0;
            }
        }
    }

} // namespace standby
