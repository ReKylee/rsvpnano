#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "screensavers/ScreensaverTypes.h"

namespace standby {

    class ReactionScreensaver final {
    public:
        void reset(uint16_t columns, uint16_t rows);
        void seed(uint32_t rngSeed);
        void step();
        Frame frame() const;

    private:
        static constexpr uint8_t kStateCount = 12; // cyclic phases of the BZ-inspired reaction
        static constexpr uint8_t kDimStateEnd = 4;

        void render();

        uint16_t columns_ = 0;
        uint16_t rows_ = 0;
        size_t cellCount_ = 0;
        size_t wordCount_ = 0;
        uint32_t rng_ = 1;
        uint32_t generation_ = 0;
        bool fullRedraw_ = true;
        std::array<uint8_t, kMaxStandbyCells> states_{};
        std::array<uint8_t, kMaxStandbyCells> nextStates_{};
        PackedGridStorage cells_{};
        PackedGridStorage dimCells_{};
        PackedGridStorage dirtyCells_{};
    };

} // namespace standby
