#include "board/BoardPower.h"
#include <esp_log.h>

#include <Wire.h>
#include <algorithm>

#include "platforms/waveshare_amoled_216/WaveshareAmoled216.h"

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
            ESP_LOGW("board", "AXP2101 not responding");
            return false;
        }

        gPmu.enableBattDetection();
        gPmu.enableBattVoltageMeasure();

        if constexpr (WaveshareAmoled216::Axp2101Wiring::kRequiresPowerKeyConfig) {
            gPmu.setPowerKeyPressOnTime(WaveshareAmoled216::Axp2101Wiring::kPowerKeyOnTimeValue);
            gPmu.setPowerKeyPressOffTime(WaveshareAmoled216::Axp2101Wiring::kPowerKeyOffTimeValue);
            gPmu.setLongPressPowerOFF();
        }

        if constexpr (WaveshareAmoled216::Axp2101Wiring::kEnablePowerKeyIrqs) {
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
        if constexpr (!WaveshareAmoled216::Axp2101Wiring::kEnablePowerKeyIrqs) {
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
        pinMode(WaveshareAmoled216::AudioWiring::kAudioEnablePin, OUTPUT);
        digitalWrite(WaveshareAmoled216::AudioWiring::kAudioEnablePin, HIGH);
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

    Diagnostics readDiagnostics() {
        Diagnostics diagnostics = {};
        if (!ensurePmuReady()) {
            return diagnostics;
        }

        const uint16_t status = gPmu.status();
        diagnostics.available = true;
        diagnostics.externalPowerPresent = gPmu.isVbusIn();
        diagnostics.status1 = static_cast<uint8_t>(status >> 8);
        diagnostics.status2 = static_cast<uint8_t>(status & 0xFF);
        return diagnostics;
    }

    bool externalPowerPresent() {
        return ensurePmuReady() && gPmu.isVbusIn();
    }

    bool powerOff() {
        if (!ensurePmuReady()) {
            return false;
        }
        ESP_LOGD("board", "AXP2101 shutdown requested");
        gPmu.shutdown();
        return true;
    }

    bool powerButtonHeld() {
        pollPowerKeyIfDue();
        return gPowerButtonHeld;
    }

} // namespace Board::Power
