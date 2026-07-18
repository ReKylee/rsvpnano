#include "standby/VoronoiScreensaver.h"

#include <algorithm>
#include <climits>

namespace standby {

    void VoronoiScreensaver::reset(uint16_t columns, uint16_t rows) {
        columns_ = std::min<uint16_t>(columns, kMaxStandbyColumns);
        rows_ = std::min<uint16_t>(rows, kMaxStandbyRows);
        wordCount_ = packedWordCount(static_cast<size_t>(columns_) * rows_);
    }

    void VoronoiScreensaver::seed(uint32_t rngSeed) {
        rng_ = rngSeed == 0 ? 1U : rngSeed;
        generation_ = 0;
        fullRedraw_ = true;
        const uint16_t safeColumns = std::max<uint16_t>(1, columns_);
        const uint16_t safeRows = std::max<uint16_t>(1, rows_);
        for (size_t i = 0; i < kSiteCount; ++i) {
            vx_[i] = static_cast<int16_t>(((advanceRng(rng_) >> 8) % safeColumns) * 16);
            vy_[i] = static_cast<int16_t>(((advanceRng(rng_) >> 8) % safeRows) * 16);
            const int16_t dx = static_cast<int16_t>(4 + ((advanceRng(rng_) >> 24) % 7));
            const int16_t dy = static_cast<int16_t>(3 + ((advanceRng(rng_) >> 24) % 6));
            vdx_[i] = (advanceRng(rng_) & 1U) != 0 ? dx : static_cast<int16_t>(-dx);
            vdy_[i] = (advanceRng(rng_) & 1U) != 0 ? dy : static_cast<int16_t>(-dy);
        }
        render();
    }

    void VoronoiScreensaver::step() {
        fullRedraw_ = false;
        const int16_t maxX = static_cast<int16_t>((std::max<uint16_t>(1, columns_) - 1U) * 16);
        const int16_t maxY = static_cast<int16_t>((std::max<uint16_t>(1, rows_) - 1U) * 16);
        for (size_t i = 0; i < kSiteCount; ++i) {
            int16_t nextX = static_cast<int16_t>(vx_[i] + vdx_[i]);
            int16_t nextY = static_cast<int16_t>(vy_[i] + vdy_[i]);
            if (nextX < 0 || nextX > maxX) {
                vdx_[i] = static_cast<int16_t>(-vdx_[i]);
                nextX = std::clamp<int16_t>(nextX, 0, maxX);
            }
            if (nextY < 0 || nextY > maxY) {
                vdy_[i] = static_cast<int16_t>(-vdy_[i]);
                nextY = std::clamp<int16_t>(nextY, 0, maxY);
            }
            vx_[i] = nextX;
            vy_[i] = nextY;
        }

        ++generation_;
        if (generation_ > 2400) {
            seed(advanceRng(rng_));
            return;
        }
        render();
    }

    Frame VoronoiScreensaver::frame() const {
        return Frame{viewOf(cells_, wordCount_), viewOf(dimCells_, wordCount_), viewOf(dirtyCells_, wordCount_),
                     generation_, fullRedraw_};
    }

    void VoronoiScreensaver::render() {
        const size_t cellCount = static_cast<size_t>(columns_) * rows_;
        if (cellCount == 0) {
            clearPackedGrid(cells_, wordCount_);
            clearPackedGrid(dimCells_, wordCount_);
            clearPackedGrid(dirtyCells_, wordCount_);
            return;
        }

        std::array<int32_t, kSiteCount> initialDx;
        std::array<int32_t, kSiteCount> initialDxSquared;
        std::array<int32_t, kSiteCount> dy;
        std::array<int32_t, kSiteCount> dySquared;
        for (size_t i = 0; i < kSiteCount; ++i) {
            initialDx[i] = 8 - vx_[i];
            initialDxSquared[i] = initialDx[i] * initialDx[i];
            dy[i] = 8 - vy_[i];
            dySquared[i] = dy[i] * dy[i];
        }

        uint32_t brightWord = 0;
        uint32_t dimWord = 0;
        for (uint16_t y = 0; y < rows_; ++y) {
            std::array<int32_t, kSiteCount> dx = initialDx;
            std::array<int32_t, kSiteCount> dxSquared = initialDxSquared;
            for (uint16_t x = 0; x < columns_; ++x) {
                int32_t nearest = INT_MAX;
                int32_t secondNearest = INT_MAX;
                for (size_t i = 0; i < kSiteCount; ++i) {
                    const int32_t distance = dxSquared[i] + dySquared[i];
                    if (distance < nearest) {
                        secondNearest = nearest;
                        nearest = distance;
                    } else if (distance < secondNearest) {
                        secondNearest = distance;
                    }
                    dxSquared[i] += 32 * dx[i] + 256;
                    dx[i] += 16;
                }

                const size_t cellIndex = static_cast<size_t>(y) * columns_ + x;
                const uint32_t mask = 1UL << (cellIndex % kPackedBitsPerWord);
                const int32_t gap = secondNearest - nearest;
                if (nearest < 1200 || gap < 190)
                    brightWord |= mask;
                else if (gap < 580 + nearest / 180)
                    dimWord |= mask;

                if ((cellIndex % kPackedBitsPerWord) == kPackedBitsPerWord - 1U || cellIndex + 1U == cellCount) {
                    const size_t word = cellIndex / kPackedBitsPerWord;
                    dirtyCells_[word] = (cells_[word] ^ brightWord) | (dimCells_[word] ^ dimWord);
                    cells_[word] = brightWord;
                    dimCells_[word] = dimWord;
                    brightWord = 0;
                    dimWord = 0;
                }
            }

            for (size_t i = 0; i < kSiteCount; ++i) {
                dySquared[i] += 32 * dy[i] + 256;
                dy[i] += 16;
            }
        }
    }

} // namespace standby
