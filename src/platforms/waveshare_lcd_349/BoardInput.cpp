#include "board/BoardInput.h"

#include <array>

#include <Wire.h>
#include <esp_log.h>

#include "drivers/gpio/tca9554/Tca9554.h"
#include "drivers/touch/axs15231b_touch/axs15231b_touch.h"
#include "platforms/waveshare_lcd_349/WaveshareLcd349.h"

namespace {

    TwoWire& touchWire() {
        return Wire;
    }

    bool primaryPressedRaw() {
        if constexpr (WaveshareLcd349::Buttons::kBootPin < 0) {
            return false;
        }
        return !digitalRead(WaveshareLcd349::Buttons::kBootPin);
    }

    bool powerPressedRaw() {
        if constexpr (WaveshareLcd349::Buttons::kPowerPin < 0) {
            return false;
        }
        return !digitalRead(WaveshareLcd349::Buttons::kPowerPin);
    }

    void configureButtonPins() {
        if constexpr (WaveshareLcd349::Buttons::kBootPin >= 0) {
            pinMode(WaveshareLcd349::Buttons::kBootPin, INPUT_PULLUP);
        }
        if constexpr (WaveshareLcd349::Buttons::kPowerPin >= 0) {
            pinMode(WaveshareLcd349::Buttons::kPowerPin, INPUT_PULLUP);
        }
    }

} // namespace

namespace Board::Input {

    bool begin() {
        configureButtonPins();
        return true;
    }

    void end() {}

    void cancel() {}

    ::Input::ControlTiming controlTiming() {
        return {};
    }

    ::Input::PressActions currentActions() {
        ::Input::PressActions actions = {};
        const bool primaryPressed = primaryPressedRaw();
        const bool powerPressed = powerPressedRaw();
        if (primaryPressed) {
            actions.shortPress |= ::Input::ActionSelect | ::Input::ActionPlayPause;
            actions.longPress |= ::Input::ActionStandby;
        }
        if (powerPressed) {
            actions.shortPress |= ::Input::ActionOpenMenu | ::Input::ActionBack;
            actions.longPress |= ::Input::ActionPowerOff;
        }
        return actions;
    }

    ui::TouchSurface touchSurface() {
        return {WaveshareLcd349::DisplayWiring::kPanelWidth, WaveshareLcd349::DisplayWiring::kPanelHeight};
    }

    ::Input::TouchTiming touchTiming() {
        return {
            .readyPollIntervalMs = WaveshareLcd349::TouchWiring::kReadyPollIntervalMs,
            .pollIntervalMs = WaveshareLcd349::TouchWiring::kPollIntervalMs,
        };
    }

    bool beginTouch() {
        return Axs15231bTouch::probe(touchWire(), WaveshareLcd349::TouchWiring::kAddress);
    }

    bool touchReady() {
        static bool readFailureLogged = false;
        bool high = true;
        if (!BoardDrivers::Tca9554::readInputPin(Wire1, WaveshareLcd349::Tca9554Wiring::kAddress,
                                                 WaveshareLcd349::Tca9554Wiring::kTouchInterruptPin, high,
                                                 WaveshareLcd349::Tca9554Wiring::kReleaseBusBeforeRead)) {
            if (!readFailureLogged)
                ESP_LOGW("input", "EXIO0 read failed");
            readFailureLogged = true;
            return false;
        }
        if (readFailureLogged)
            ESP_LOGI("input", "EXIO0 reads recovered");
        readFailureLogged = false;
        return !high;
    }

    bool readTouch(ui::TouchContact& contact) {
        std::array<uint8_t, Axs15231bTouch::kPacketLength> data = {};
        const bool read =
            Axs15231bTouch::readPacket(touchWire(), WaveshareLcd349::TouchWiring::kAddress, data.data(), data.size());
        if (!read) {
            return false;
        }

        BoardDrivers::Touch::Sample decoded = {};
        if (!Axs15231bTouch::decodePacket(data.data(), data.size(), WaveshareLcd349::DisplayWiring::kPanelWidth,
                                          WaveshareLcd349::DisplayWiring::kPanelHeight, decoded)) {
            return false;
        }

        contact = {decoded.touched, decoded.physicalX, decoded.physicalY};
        return true;
    }

} // namespace Board::Input
