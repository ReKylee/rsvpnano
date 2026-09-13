#pragma once

#include "drivers/imu/qmi8658/Qmi8658.h"

namespace BoardPlatform {
    // Defined once by the selected board; construction performs no peripheral I/O.
    BoardDrivers::Qmi8658::Device& qmi8658();
} // namespace BoardPlatform
