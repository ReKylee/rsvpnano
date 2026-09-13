#include "platforms/common/ImuBinding.h"
#include "platforms/waveshare_c6_touch_lcd_147/WaveshareC6TouchLcd147.h"
#include <Wire.h>

namespace BoardPlatform {
    BoardDrivers::Qmi8658::Device& qmi8658() {
        static BoardDrivers::Qmi8658::Device device{
            Wire, {WaveshareC6TouchLcd147::ImuWiring::kAddress,
                    WaveshareC6TouchLcd147::ImuWiring::kReleaseBusBeforeRead, "Wire"}};
        return device;
    }
} // namespace BoardPlatform
