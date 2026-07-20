#pragma once

#include <Arduino.h>

#include "board/BoardTypes.h"

namespace Board::Power {

    using BatteryStatus = Board::BatteryStatus;
    using DiagnosticSnapshot = Board::PowerDiagnosticSnapshot;

    void begin();
    bool enableAudioPowerIfAvailable();
    bool readBatteryStatus(BatteryStatus& status);
    DiagnosticSnapshot diagnosticSnapshot();
    bool externalPowerPresent();
    bool powerOff();
    bool powerButtonHeld();

} // namespace Board::Power
