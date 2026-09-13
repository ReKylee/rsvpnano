#include "board/BoardImu.h"

// A generic interface include must not instantiate or link a particular device.
// This fixture intentionally has no BoardConfig, Wire object or QMI8658 implementation.
int main() {
    constexpr Board::Imu::Acceleration sample{};
    static_assert(sample.x == 0 && sample.y == 0 && sample.z == 0);
}
