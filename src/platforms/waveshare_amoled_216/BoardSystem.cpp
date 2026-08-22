#include "board/BoardSystem.h"
#include <esp_log.h>
#include "board/BoardPower.h"
#include "logging/Logger.h"

#include <Wire.h>

#include "platforms/waveshare_amoled_216/WaveshareAmoled216.h"

namespace Board {

    namespace {

        void beginWire(TwoWire& wire, int sda, int scl, uint32_t clockHz, uint32_t timeoutMs) {
            if (sda < 0 || scl < 0) {
                return;
            }

            wire.begin(sda, scl);
            wire.setClock(clockHz);
            wire.setTimeOut(timeoutMs);
        }

    } // namespace

    namespace System {

        void begin() {
            if constexpr (WaveshareAmoled216::Buttons::kBootPin >= 0) {
                pinMode(WaveshareAmoled216::Buttons::kBootPin, INPUT_PULLUP);
            }
            if constexpr (WaveshareAmoled216::Buttons::kPowerPin >= 0) {
                pinMode(WaveshareAmoled216::Buttons::kPowerPin, INPUT_PULLUP);
            }
            if constexpr (WaveshareAmoled216::System::kTouchIrqPin >= 0) {
                pinMode(WaveshareAmoled216::System::kTouchIrqPin, INPUT_PULLUP);
            }
            beginWire(Wire, WaveshareAmoled216::System::kTouchSdaPin, WaveshareAmoled216::System::kTouchSclPin,
                      WaveshareAmoled216::System::kTouchI2cClockHz, WaveshareAmoled216::System::kTouchI2cTimeoutMs);

            Board::Power::begin();
        }

        EspLightSleep::WakeReason lightSleep(uint32_t timeoutMs) {
            return EspLightSleep::wait<WaveshareAmoled216::System::kLightSleepWakeGpio,
                                       WaveshareAmoled216::System::kTouchIrqPin>(timeoutMs);
        }

        const char* wakeLabel(bool externalPowerPresent) {
            return externalPowerPresent ? "Press PWR to wake" : "Press PWR to start";
        }

        void logStartupDiagnostics() {
            Logger::logResetReason();

            const Board::Power::Diagnostics power = Board::Power::readDiagnostics();
            if (!power.available) {
                ESP_LOGW("diag", "power_diagnostics=unavailable");
                return;
            }

            ESP_LOGD("diag", "power_diagnostics=vbus:%u axp_status1:0x%02X axp_status2:0x%02X axp_pwr_irq:0x%02X",
                     power.externalPowerPresent ? 1 : 0, power.status1, power.status2, power.powerKeyIrqStatus);
        }

    } // namespace System

} // namespace Board
