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
        fullRedraw_ = true;
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
        return Frame{viewOf(cells_, wordCount_), viewOf(dimCells_, wordCount_), {}, generation_, fullRedraw_};
    }

    void VoronoiScreensaver::render() {
        clearPackedGrid(cells_, wordCount_);
        clearPackedGrid(dimCells_, wordCount_);

        for (uint16_t y = 0; y < rows_; ++y) {
            const int32_t cellY = static_cast<int32_t>(y) * 16 + 8;
            for (uint16_t x = 0; x < columns_; ++x) {
                const int32_t cellX = static_cast<int32_t>(x) * 16 + 8;
                int32_t nearest = INT_MAX;
                int32_t secondNearest = INT_MAX;
                for (size_t i = 0; i < kSiteCount; ++i) {
                    const int32_t dx = cellX - vx_[i];
                    const int32_t dy = cellY - vy_[i];
                    const int32_t distance = dx * dx + dy * dy;
                    if (distance < nearest) {
                        secondNearest = nearest;
                        nearest = distance;
                    } else if (distance < secondNearest) {
                        secondNearest = distance;
                    }
                }

                const size_t cellIndex = static_cast<size_t>(y) * columns_ + x;
                const int32_t gap = secondNearest - nearest;
                if (nearest < 1200 || gap < 190) {
                    setCell(cells_, cellIndex, true);
                } else if (gap < 580 + nearest / 180) {
                    setCell(dimCells_, cellIndex, true);
                }
            }
        }
    }

} // namespace standby
