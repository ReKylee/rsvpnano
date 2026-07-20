#pragma once

#include <Arduino.h>

#include "sleep/EspLightSleep.h"

namespace Board::System {

    void begin();
    EspLightSleep::WakeReason lightSleep(uint32_t timeoutMs);
    const char* wakeLabel(bool externalPowerPresent);
    void logStartupDiagnostics();

} // namespace Board::System
