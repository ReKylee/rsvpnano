#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "standby/ScreensaverTypes.h"

namespace standby {

    class VoronoiScreensaver final {
    public:
        void reset(uint16_t columns, uint16_t rows);
        void seed(uint32_t rngSeed);
        void step();
        Frame frame() const;

    private:
        static constexpr size_t kSiteCount = 15;

        void render();

        uint16_t columns_ = 0;
        uint16_t rows_ = 0;
        uint32_t rng_ = 1;
        uint32_t generation_ = 0;
        bool fullRedraw_ = true;
        size_t wordCount_ = 0;
        PackedGridStorage cells_{};
        PackedGridStorage dimCells_{};
        PackedGridStorage dirtyCells_{};
        std::array<int16_t, kSiteCount> vx_{};
        std::array<int16_t, kSiteCount> vy_{};
        std::array<int16_t, kSiteCount> vdx_{};
        std::array<int16_t, kSiteCount> vdy_{};
    };

} // namespace standby
