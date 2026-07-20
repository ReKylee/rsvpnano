#include "board/BoardPower.h"
#include "logging/Logger.h"

#include <Wire.h>
#include <algorithm>

#include "platforms/waveshare_amoled_206/WaveshareAmoled206.h"

#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>

namespace {

    constexpr uint32_t kPowerKeyPollIntervalMs = 20;
    XPowersAXP2101 gPmu;
    bool gPmuReady = false;
    bool gPowerButtonHeld = false;
    uint32_t gLastPowerKeyPollMs = 0;

    bool beginPmu() {
        gPmuReady = gPmu.init(Wire);
        if (!gPmuReady) {
            Logger::warning("board", "AXP2101 not responding");
            return false;
        }

        if constexpr (WaveshareAmoled206::Axp2101Wiring::kRequiresPowerKeyConfig) {
            gPmu.setPowerKeyPressOnTime(WaveshareAmoled206::Axp2101Wiring::kPowerKeyOnTimeValue);
            gPmu.setPowerKeyPressOffTime(WaveshareAmoled206::Axp2101Wiring::kPowerKeyOffTimeValue);
            gPmu.setLongPressPowerOFF();
        }

        if constexpr (WaveshareAmoled206::Axp2101Wiring::kEnablePowerKeyIrqs) {
            gPmu.enableIRQ(XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ | XPOWERS_AXP2101_PKEY_POSITIVE_IRQ);
            gPmu.clearIrqStatus();
        }

        gPowerButtonHeld = false;
        gLastPowerKeyPollMs = 0;
        return true;
    }

    bool ensurePmuReady() {
        return gPmuReady || beginPmu();
    }

    void pollPowerKeyIfDue(bool force = false) {
        if constexpr (!WaveshareAmoled206::Axp2101Wiring::kEnablePowerKeyIrqs) {
            return;
        }

        const uint32_t nowMs = millis();
        if (!force && nowMs - gLastPowerKeyPollMs < kPowerKeyPollIntervalMs) {
            return;
        }
        gLastPowerKeyPollMs = nowMs;

        if (!ensurePmuReady()) {
            return;
        }

        gPmu.getIrqStatus();
        if (gPmu.isPekeyNegativeIrq()) {
            gPowerButtonHeld = true;
        }
        if (gPmu.isPekeyPositiveIrq()) {
            gPowerButtonHeld = false;
        }
        gPmu.clearIrqStatus();
    }

} // namespace

namespace Board::Power {

    void begin() {
        beginPmu();
    }

    bool enableAudioPowerIfAvailable() {
        pinMode(WaveshareAmoled206::AudioWiring::kAudioEnablePin, OUTPUT);
        digitalWrite(WaveshareAmoled206::AudioWiring::kAudioEnablePin, HIGH);
        return true;
    }

    bool readBatteryStatus(BatteryStatus& status) {
        status = {};
        if (!ensurePmuReady() || !gPmu.isBatteryConnect()) {
            return false;
        }

        status.present = true;
        status.voltage = static_cast<float>(gPmu.getBattVoltage()) / 1000.0f;
        const int percent = gPmu.getBatteryPercent();
        status.percent = static_cast<uint8_t>(std::clamp(percent, 0, 100));
        return status.voltage > 0.0f;
    }

    DiagnosticSnapshot diagnosticSnapshot() {
        DiagnosticSnapshot snapshot = {};
        if (!ensurePmuReady()) {
            return snapshot;
        }

        const uint16_t status = gPmu.status();
        snapshot.available = true;
        snapshot.externalPowerPresent = gPmu.isVbusIn();
        snapshot.status1 = static_cast<uint8_t>(status >> 8);
        snapshot.status2 = static_cast<uint8_t>(status & 0xFF);
        return snapshot;
    }

    bool externalPowerPresent() {
        return ensurePmuReady() && gPmu.isVbusIn();
    }

    bool powerOff() {
        if (!ensurePmuReady()) {
            return false;
        }
        Logger::debug("board", "AXP2101 shutdown requested");
        gPmu.shutdown();
        return true;
    }

    bool powerButtonHeld() {
        pollPowerKeyIfDue();
        return gPowerButtonHeld;
    }

} // namespace Board::Power
