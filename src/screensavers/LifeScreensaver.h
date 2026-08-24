#pragma once

#include <cstddef>
#include <cstdint>

#include "screensavers/LifeGrid.h"
#include "screensavers/ScreensaverTypes.h"

namespace standby {

    class LifeScreensaver final {
    public:
        void reset(uint16_t columns, uint16_t rows);
        void seed(uint32_t rngSeed);
        void step();
        Frame frame() const;

    private:
        void seedCrossfireScene();
        void seedOscillatorGardenScene();
        void seedMethuselahBurstScene();
        bool hasDirtyCells() const;

        uint16_t columns_ = 0;
        uint16_t rows_ = 0;
        uint32_t rng_ = 1;
        uint32_t generation_ = 0;
        uint16_t stableFrames_ = 0;
        uint8_t sceneIndex_ = 0;
        IncrementalLifeGrid life_;
    };

} // namespace standby
