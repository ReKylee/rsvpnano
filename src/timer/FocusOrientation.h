#pragma once

#include <cstdint>

#include "timer/FocusSession.h"

namespace focus {

    class OrientationReader {
    public:
        bool begin();
        Orientation update(uint32_t nowMs);
        bool available() const { return available_; }
        Orientation orientation() const { return stable_; }

    private:
        bool updateRegister(uint8_t reg, uint8_t mask, uint8_t value);
        bool read(float& x, float& y, float& z);
        static Orientation classify(float x, float y, float z);

        bool available_ = false;
        uint8_t address_ = 0;
        float scale_ = 4.0f / 32768.0f;
        Orientation candidate_ = Orientation::Unknown;
        Orientation stable_ = Orientation::Unknown;
        uint32_t candidateSinceMs_ = 0;
        uint32_t lastSampleMs_ = 0;
    };

} // namespace focus
