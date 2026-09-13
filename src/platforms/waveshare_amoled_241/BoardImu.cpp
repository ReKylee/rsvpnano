#include "platforms/common/ImuBinding.h"
#include "platforms/waveshare_amoled_241/WaveshareAmoled241.h"
#include <Wire.h>

namespace BoardPlatform {
    BoardDrivers::Qmi8658::Device& qmi8658() {
        static BoardDrivers::Qmi8658::Device device{
            Wire1, {WaveshareAmoled241::ImuWiring::kAddress,
                    WaveshareAmoled241::ImuWiring::kReleaseBusBeforeRead, "Wire1"}};
        return device;
    }
} // namespace BoardPlatform
