#include "board/BoardSystem.h"
#include "board/BoardPower.h"
#include "logging/Logger.h"

#include <Wire.h>

#include "platforms/waveshare_amoled_18/WaveshareAmoled18.h"

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
            if constexpr (WaveshareAmoled18::Buttons::kBootPin >= 0) {
                pinMode(WaveshareAmoled18::Buttons::kBootPin, INPUT_PULLUP);
            }
            if constexpr (WaveshareAmoled18::Buttons::kPowerPin >= 0) {
                pinMode(WaveshareAmoled18::Buttons::kPowerPin, INPUT_PULLUP);
            }
            if constexpr (WaveshareAmoled18::System::kTouchIrqPin >= 0) {
                pinMode(WaveshareAmoled18::System::kTouchIrqPin, INPUT_PULLUP);
            }
            beginWire(Wire, WaveshareAmoled18::System::kTouchSdaPin, WaveshareAmoled18::System::kTouchSclPin,
                      WaveshareAmoled18::System::kTouchI2cClockHz, WaveshareAmoled18::System::kTouchI2cTimeoutMs);

            Board::Power::begin();
        }

        EspLightSleep::WakeReason lightSleep(uint32_t timeoutMs) {
            return EspLightSleep::wait<WaveshareAmoled18::System::kLightSleepWakeGpio,
                                       WaveshareAmoled18::System::kTouchIrqPin>(timeoutMs);
        }

        const char* wakeLabel(bool externalPowerPresent) {
            return externalPowerPresent ? "Press PWR to wake" : "Press PWR to start";
        }

        void logStartupDiagnostics() {
            Logger::logResetReason();

            const Board::Power::DiagnosticSnapshot power = Board::Power::diagnosticSnapshot();
            if (!power.available) {
                Logger::warning("diag", "power_snapshot=unavailable");
                return;
            }

            Logger::debug("diag", "power_snapshot=vbus:%u axp_status1:0x%02X axp_status2:0x%02X axp_pwr_irq:0x%02X",
                          power.externalPowerPresent ? 1 : 0, power.status1, power.status2, power.powerKeyIrqStatus);
        }

    } // namespace System

} // namespace Board
