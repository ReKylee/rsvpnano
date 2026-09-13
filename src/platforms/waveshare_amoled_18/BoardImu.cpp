#include "platforms/common/ImuBinding.h"
#include "platforms/waveshare_amoled_18/WaveshareAmoled18.h"
#include <Wire.h>

namespace BoardPlatform {
    BoardDrivers::Qmi8658::Device& qmi8658() {
        static BoardDrivers::Qmi8658::Device device{
            Wire, {WaveshareAmoled18::ImuWiring::kAddress,
                    WaveshareAmoled18::ImuWiring::kReleaseBusBeforeRead, "Wire"}};
        return device;
    }
} // namespace BoardPlatform
