#include "platforms/common/ImuBinding.h"
#include "platforms/waveshare_amoled_216/WaveshareAmoled216.h"
#include <Wire.h>

namespace BoardPlatform {
    BoardDrivers::Qmi8658::Device& qmi8658() {
        static BoardDrivers::Qmi8658::Device device{
            Wire, {WaveshareAmoled216::ImuWiring::kAddress,
                    WaveshareAmoled216::ImuWiring::kReleaseBusBeforeRead, "Wire"}};
        return device;
    }
} // namespace BoardPlatform
