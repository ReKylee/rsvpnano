#pragma once

#include <Arduino.h>

namespace Board::Power {

    struct BatteryStatus {
        bool present = false;
        float voltage = 0.0f;
        uint8_t percent = 0;
    };

    struct DiagnosticSnapshot {
        bool available = false;
        bool externalPowerPresent = false;
        uint8_t status1 = 0;
        uint8_t status2 = 0;
        uint8_t powerKeyIrqStatus = 0;
    };

    struct BatteryState {
        BatteryStatus status;
        uint32_t sampledAtMs = 0;
        bool charging = false;
    };

    void begin();
    bool enableAudioPowerIfAvailable();
    bool readBatteryStatus(BatteryStatus& status);
    DiagnosticSnapshot diagnosticSnapshot();
    bool externalPowerPresent();
    bool powerOff();
    bool powerButtonHeld();

    inline void updateBattery(BatteryState& battery, uint32_t nowMs, bool force = false) {
        constexpr uint32_t kSampleIntervalMs = 120000;
        if (!force && nowMs - battery.sampledAtMs < kSampleIntervalMs)
            return;
        battery.sampledAtMs = nowMs;
        if (readBatteryStatus(battery.status))
            battery.charging = externalPowerPresent();
    }

} // namespace Board::Power
