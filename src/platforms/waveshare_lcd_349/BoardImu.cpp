#include "platforms/common/ImuBinding.h"
#include "platforms/waveshare_lcd_349/WaveshareLcd349.h"
#include <Wire.h>

namespace BoardPlatform {
    BoardDrivers::Qmi8658::Device& qmi8658() {
        static BoardDrivers::Qmi8658::Device device{
            Wire1, {WaveshareLcd349::ImuWiring::kAddress,
                    WaveshareLcd349::ImuWiring::kReleaseBusBeforeRead, "Wire1"}};
        return device;
    }
} // namespace BoardPlatform
