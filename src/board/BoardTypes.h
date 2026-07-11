#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Board {

    struct BatteryStatus {
        bool present = false;
        float voltage = 0.0f;
        uint8_t percent = 0;
    };

    struct PowerDiagnosticSnapshot {
        bool available = false;
        bool externalPowerPresent = false;
        uint8_t status1 = 0;
        uint8_t status2 = 0;
        uint8_t powerKeyIrqStatus = 0;
    };

} // namespace Board
