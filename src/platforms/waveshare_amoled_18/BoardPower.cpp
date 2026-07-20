#include "board/BoardPower.h"
#include <esp_log.h>

#include <Wire.h>
#include <algorithm>

#include "drivers/gpio/tca9554/Tca9554.h"
#include "platforms/waveshare_amoled_18/WaveshareAmoled18.h"

#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>

namespace {

    constexpr uint32_t kPowerKeyPollIntervalMs = 20;
    XPowersAXP2101 gPmu;
    bool gPmuReady = false;
    bool gPowerButtonHeld = false;
    uint32_t gLastPowerKeyPollMs = 0;

    void configureIoExpander() {
        BoardDrivers::Tca9554::PortState state = {};
        if (!BoardDrivers::Tca9554::readPortState(Wire1, WaveshareAmoled18::Tca9554Wiring::kAddress, state,
                                                  WaveshareAmoled18::Tca9554Wiring::kReleaseBusBeforeRead)) {
            ESP_LOGW("board", "TCA9554 not detected");
            return;
        }

        state.output &= WaveshareAmoled18::Tca9554Wiring::kDisplayClearMask;
        state.output |= WaveshareAmoled18::Tca9554Wiring::kSdEnableMask;
        state.config &= WaveshareAmoled18::Tca9554Wiring::kOutputClearMask;
        state.config |= WaveshareAmoled18::Tca9554Wiring::kInputMask;

        if (!BoardDrivers::Tca9554::writePortState(Wire1, WaveshareAmoled18::Tca9554Wiring::kAddress, state)) {
            ESP_LOGE("board", "TCA9554 output setup failed");
            return;
        }
    }

    bool beginPmu() {
        gPmuReady = gPmu.init(Wire);
        if (!gPmuReady) {
            ESP_LOGW("board", "AXP2101 not responding");
            return false;
        }

        if constexpr (WaveshareAmoled18::Axp2101Wiring::kRequiresPowerKeyConfig) {
            gPmu.setPowerKeyPressOnTime(WaveshareAmoled18::Axp2101Wiring::kPowerKeyOnTimeValue);
            gPmu.setPowerKeyPressOffTime(WaveshareAmoled18::Axp2101Wiring::kPowerKeyOffTimeValue);
            gPmu.setLongPressPowerOFF();
        }

        if constexpr (WaveshareAmoled18::Axp2101Wiring::kEnablePowerKeyIrqs) {
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
        if constexpr (!WaveshareAmoled18::Axp2101Wiring::kEnablePowerKeyIrqs) {
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
        configureIoExpander();
        beginPmu();
    }

    bool enableAudioPowerIfAvailable() {
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
        ESP_LOGD("board", "AXP2101 shutdown requested");
        gPmu.shutdown();
        return true;
    }

    bool powerButtonHeld() {
        pollPowerKeyIfDue();
        return gPowerButtonHeld;
    }

} // namespace Board::Power
