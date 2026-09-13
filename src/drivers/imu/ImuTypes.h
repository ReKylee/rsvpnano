#pragma once

namespace BoardDrivers::Imu {
    // Acceleration in g, in the sensor's existing physical axes.
    struct Acceleration {
        float x = 0;
        float y = 0;
        float z = 0;
    };
} // namespace BoardDrivers::Imu
