#include "platforms/waveshare_amoled_18/BoardDisplayPower.h"
#include <esp_log.h>

#include <Arduino.h>
#include <Wire.h>

#include "drivers/gpio/tca9554/Tca9554.h"
#include "platforms/waveshare_amoled_18/WaveshareAmoled18.h"

namespace WaveshareAmoled18::DisplayPower {

    void releaseHardware() {
        BoardDrivers::Tca9554::PortState state = {};
        if (!BoardDrivers::Tca9554::readPortState(Wire, Tca9554Wiring::kAddress, state,
                                                  Tca9554Wiring::kReleaseBusBeforeRead)) {
            ESP_LOGW("board", "TCA9554 not detected");
            return;
        }

        state.output &= Tca9554Wiring::kDisplayClearMask;
        state.config &= Tca9554Wiring::kOutputClearMask;
        state.config |= Tca9554Wiring::kInputMask;
        if (!BoardDrivers::Tca9554::writePortState(Wire, Tca9554Wiring::kAddress, state)) {
            ESP_LOGE("board", "TCA9554 display hold failed");
            return;
        }

        delay(20);
        state.output |= Tca9554Wiring::kDisplayMask;
        if (!BoardDrivers::Tca9554::writeOutput(Wire, Tca9554Wiring::kAddress, state.output)) {
            ESP_LOGE("board", "TCA9554 display release failed");
            return;
        }
        delay(50);
    }

} // namespace WaveshareAmoled18::DisplayPower
