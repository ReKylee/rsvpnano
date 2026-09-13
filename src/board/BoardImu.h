#pragma once

#include <cstdint>
#include "drivers/imu/ImuTypes.h"

namespace Board::Imu {
    using Acceleration = BoardDrivers::Imu::Acceleration;

    bool begin();
    bool readAcceleration(Acceleration& sample);
    const char* wireName();
    uint8_t address();
} // namespace Board::Imu
