#include "platforms/common/ImuBinding.h"
#include "board/BoardImu.h"

namespace Board::Imu {
    bool begin() {
        return BoardPlatform::qmi8658().begin();
    }

    bool readAcceleration(Acceleration& sample) {
        return BoardPlatform::qmi8658().readAcceleration(sample);
    }

    const char* wireName() {
        return BoardPlatform::qmi8658().wireName();
    }

    uint8_t address() {
        return BoardPlatform::qmi8658().address();
    }
} // namespace Board::Imu
