#include "board/BoardSystem.h"
#include <esp_log.h>
#include "board/BoardPower.h"
#include "logging/Logger.h"

#include <Wire.h>

#include "platforms/waveshare_c6_touch_lcd_147/WaveshareC6TouchLcd147.h"

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
            if constexpr (WaveshareC6TouchLcd147::Buttons::kBootPin >= 0) {
                pinMode(WaveshareC6TouchLcd147::Buttons::kBootPin, INPUT_PULLUP);
            }
            if constexpr (WaveshareC6TouchLcd147::System::kTouchIrqPin >= 0) {
                pinMode(WaveshareC6TouchLcd147::System::kTouchIrqPin, INPUT_PULLUP);
            }

            beginWire(Wire, WaveshareC6TouchLcd147::System::kSystemI2cSdaPin,
                      WaveshareC6TouchLcd147::System::kSystemI2cSclPin,
                      WaveshareC6TouchLcd147::System::kSystemI2cClockHz,
                      WaveshareC6TouchLcd147::System::kSystemI2cTimeoutMs);

            Board::Power::begin();
        }

        EspLightSleep::WakeReason lightSleep(uint32_t timeoutMs) {
            return EspLightSleep::wait<WaveshareC6TouchLcd147::System::kLightSleepWakeGpio,
                                       WaveshareC6TouchLcd147::System::kTouchIrqPin>(timeoutMs);
        }

        const char* wakeLabel(bool) {
            return "Press BOOT to start";
        }

        void logStartupDiagnostics() {
            Logger::logResetReason();

            const Board::Power::Diagnostics power = Board::Power::readDiagnostics();
            if (!power.available) {
                ESP_LOGW("diag", "power_diagnostics=unavailable");
                return;
            }

            ESP_LOGD("diag", "power_diagnostics=vbus:%u", power.externalPowerPresent ? 1 : 0);
        }

    } // namespace System

} // namespace Board
