#pragma once

#include <cstdint>
#include "focus/FocusSession.h"

namespace focus {
    class OrientationReader {
    public:
        bool begin();
        Orientation update(uint32_t nowMs);
        bool available() const { return available_; }
        Orientation orientation() const { return stable_; }

    private:
        static Orientation classify(float x, float y, float z);

        bool available_ = false;
        Orientation candidate_ = Orientation::Unknown;
        Orientation stable_ = Orientation::Unknown;
        uint32_t candidateSinceMs_ = 0;
        uint32_t lastSampleMs_ = 0;
    };
} // namespace focus
