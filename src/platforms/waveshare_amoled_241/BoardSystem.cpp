#include "board/BoardSystem.h"
#include "board/BoardPower.h"
#include "logging/Logger.h"

#include <Wire.h>

#include "platforms/waveshare_amoled_241/WaveshareAmoled241.h"

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
            if constexpr (WaveshareAmoled241::Buttons::kBootPin >= 0) {
                pinMode(WaveshareAmoled241::Buttons::kBootPin, INPUT_PULLUP);
            }
            if constexpr (WaveshareAmoled241::Buttons::kPowerPin >= 0) {
                pinMode(WaveshareAmoled241::Buttons::kPowerPin, INPUT_PULLUP);
            }
            if constexpr (WaveshareAmoled241::System::kTouchIrqPin >= 0) {
                pinMode(WaveshareAmoled241::System::kTouchIrqPin, INPUT_PULLUP);
            }
            beginWire(Wire1, WaveshareAmoled241::System::kTouchSdaPin, WaveshareAmoled241::System::kTouchSclPin,
                      WaveshareAmoled241::System::kTouchI2cClockHz, WaveshareAmoled241::System::kTouchI2cTimeoutMs);

            Board::Power::begin();
        }

        EspLightSleep::WakeReason lightSleep(uint32_t timeoutMs) {
            return EspLightSleep::wait<WaveshareAmoled241::System::kLightSleepWakeGpio,
                                       WaveshareAmoled241::System::kTouchIrqPin>(timeoutMs);
        }

        const char* wakeLabel(bool) {
            return "Hold PWR to start";
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
