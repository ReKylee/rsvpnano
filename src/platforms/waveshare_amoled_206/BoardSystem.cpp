#include "board/BoardSystem.h"
#include "board/BoardPower.h"
#include <esp_log.h>
#include "logging/Logger.h"

#include <Wire.h>

#include "platforms/waveshare_amoled_206/WaveshareAmoled206.h"

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
            if constexpr (WaveshareAmoled206::Buttons::kBootPin >= 0) {
                pinMode(WaveshareAmoled206::Buttons::kBootPin, INPUT_PULLUP);
            }
            if constexpr (WaveshareAmoled206::System::kTouchIrqPin >= 0) {
                pinMode(WaveshareAmoled206::System::kTouchIrqPin, INPUT_PULLUP);
            }
            beginWire(Wire, WaveshareAmoled206::System::kTouchSdaPin, WaveshareAmoled206::System::kTouchSclPin,
                      WaveshareAmoled206::System::kTouchI2cClockHz, WaveshareAmoled206::System::kTouchI2cTimeoutMs);

            Board::Power::begin();
        }

        EspLightSleep::WakeReason lightSleep(uint32_t timeoutMs) {
            return EspLightSleep::wait<WaveshareAmoled206::System::kLightSleepWakeGpio,
                                       WaveshareAmoled206::System::kTouchIrqPin>(timeoutMs);
        }

        const char* wakeLabel(bool externalPowerPresent) {
            return externalPowerPresent ? "Press PWR to wake" : "Press PWR to start";
        }

        void logStartupDiagnostics() {
            Logger::logResetReason();

            const Board::Power::DiagnosticSnapshot power = Board::Power::diagnosticSnapshot();
            if (!power.available) {
                ESP_LOGW("diag", "power_snapshot=unavailable");
                return;
            }

            ESP_LOGD("diag", "power_snapshot=vbus:%u axp_status1:0x%02X axp_status2:0x%02X axp_pwr_irq:0x%02X",
                          power.externalPowerPresent ? 1 : 0, power.status1, power.status2, power.powerKeyIrqStatus);
        }

    } // namespace System

} // namespace Board
