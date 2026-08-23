#include "board/BoardSystem.h"
#include <esp_log.h>
#include "board/BoardPower.h"
#include "logging/Logger.h"

#include <Wire.h>

#include "drivers/gpio/tca9554/Tca9554.h"
#include "platforms/waveshare_lcd_349/WaveshareLcd349.h"

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
            if constexpr (WaveshareLcd349::Buttons::kBootPin >= 0) {
                pinMode(WaveshareLcd349::Buttons::kBootPin, INPUT_PULLUP);
            }
            if constexpr (WaveshareLcd349::Buttons::kPowerPin >= 0) {
                pinMode(WaveshareLcd349::Buttons::kPowerPin, INPUT_PULLUP);
            }
            if constexpr (WaveshareLcd349::System::kTouchIrqPin >= 0) {
                pinMode(WaveshareLcd349::System::kTouchIrqPin, INPUT_PULLUP);
            }
            if constexpr (WaveshareLcd349::DisplayWiring::kBacklightPin >= 0) {
                pinMode(WaveshareLcd349::DisplayWiring::kBacklightPin, OUTPUT);
                digitalWrite(WaveshareLcd349::DisplayWiring::kBacklightPin, HIGH);
            }

            beginWire(Wire, WaveshareLcd349::System::kTouchSdaPin, WaveshareLcd349::System::kTouchSclPin,
                      WaveshareLcd349::System::kTouchI2cClockHz, WaveshareLcd349::System::kTouchI2cTimeoutMs);
            beginWire(Wire1, WaveshareLcd349::System::kSystemI2cSdaPin, WaveshareLcd349::System::kSystemI2cSclPin,
                      WaveshareLcd349::System::kSystemI2cClockHz, WaveshareLcd349::System::kSystemI2cTimeoutMs);

            Board::Power::begin();
        }

        EspLightSleep::WakeReason lightSleep(uint32_t timeoutMs) {
            bool ignored = true;
            if (!BoardDrivers::Tca9554::readInputPin(Wire1, WaveshareLcd349::Tca9554Wiring::kAddress,
                                                     WaveshareLcd349::Tca9554Wiring::kTouchInterruptPin, ignored,
                                                     WaveshareLcd349::Tca9554Wiring::kReleaseBusBeforeRead))
                ESP_LOGW("sleep", "failed to clear touch expander interrupt");

            constexpr gpio_num_t wakePins[] = {
                WaveshareLcd349::System::kLightSleepWakeGpio,
                static_cast<gpio_num_t>(WaveshareLcd349::System::kTouchIrqPin),
            };
            return EspLightSleep::wait(wakePins, timeoutMs);
        }

        const char* wakeLabel(bool) {
            return "Hold PWR to start";
        }

        void logStartupDiagnostics() {
            Logger::logResetReason();

            const Board::Power::Diagnostics power = Board::Power::readDiagnostics();
            if (!power.available) {
                ESP_LOGI("diag", "power_diagnostics=vbus_sense:unavailable charge_status:unavailable");
                return;
            }

            ESP_LOGD("diag", "power_diagnostics=vbus:%u axp_status1:0x%02X axp_status2:0x%02X axp_pwr_irq:0x%02X",
                     power.externalPowerPresent ? 1 : 0, power.status1, power.status2, power.powerKeyIrqStatus);
        }

    } // namespace System

} // namespace Board
