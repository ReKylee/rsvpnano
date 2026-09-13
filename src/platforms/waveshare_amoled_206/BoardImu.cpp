#include "platforms/common/ImuBinding.h"
#include "platforms/waveshare_amoled_206/WaveshareAmoled206.h"
#include <Wire.h>

namespace BoardPlatform {
    BoardDrivers::Qmi8658::Device& qmi8658() {
        static BoardDrivers::Qmi8658::Device device{
            Wire, {WaveshareAmoled206::ImuWiring::kAddress,
                    WaveshareAmoled206::ImuWiring::kReleaseBusBeforeRead, "Wire"}};
        return device;
    }
} // namespace BoardPlatform
