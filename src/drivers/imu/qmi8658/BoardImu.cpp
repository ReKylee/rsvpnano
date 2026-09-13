#include "board/BoardConfig.h"
#include "board/BoardImu.h"
#include "drivers/imu/qmi8658/Qmi8658.h"

#include <Wire.h>

namespace {
    // This adapter is selected with drivers/imu/qmi8658/**, not with the generic board facade.
    static_assert(Board::Config::IMU_I2C_BUS >= 0 && Board::Config::IMU_I2C_BUS <= 1
                  && Board::Config::IMU_I2C_BUS < SOC_I2C_NUM,
                  "Selected IMU I2C controller is unavailable");

    constexpr TwoWire& imuWire() {
#if SOC_I2C_NUM > 1
        if constexpr (Board::Config::IMU_I2C_BUS == 1)
            return Wire1;
#endif
        return Wire;
    }

    constinit BoardDrivers::Qmi8658::Device imu{
        imuWire(), {Board::Config::Imu::kAddress, Board::Config::Imu::kReleaseBusBeforeRead,
                    Board::Config::IMU_I2C_BUS == 0 ? "Wire" : "Wire1"}};
} // namespace

namespace Board::Imu {
    bool begin() {
        return imu.begin();
    }

    bool readAcceleration(Acceleration& sample) {
        return imu.readAcceleration(sample);
    }

    const char* wireName() {
        return imu.wireName();
    }

    uint8_t address() {
        return imu.address();
    }
} // namespace Board::Imu
