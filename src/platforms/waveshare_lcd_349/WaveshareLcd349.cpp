#include "platforms/waveshare_lcd_349/WaveshareLcd349.h"

#include <Wire.h>

#include "drivers/gpio/tca9554/Tca9554.h"

bool WaveshareLcd349::readTouchExpanderInterrupt(bool& active) {
    // Reading the TCA9554 Input Port samples EXIO0 and acknowledges shared EXIO_INT.
    bool touchInterruptHigh = true;
    const bool read =
        BoardDrivers::Tca9554::readInputPin(Wire1, Tca9554Wiring::kAddress, Tca9554Wiring::kTouchInterruptPin,
                                            touchInterruptHigh, Tca9554Wiring::kReleaseBusBeforeRead);
    active = read && !touchInterruptHigh;
    return read;
}
