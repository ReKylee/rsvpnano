#include "board/BoardInput.h"

#include <array>

#include <Wire.h>

#include "drivers/gpio/tca9554/Tca9554.h"
#include "drivers/touch/cst816/cst816.h"
#include "platforms/waveshare_amoled_18/WaveshareAmoled18.h"

namespace {

    TwoWire& touchWire() {
        return Wire;
    }

    bool tcaPinHeld(uint8_t pin) {
        bool levelHigh = false;
        return BoardDrivers::Tca9554::readInputPin(Wire1, WaveshareAmoled18::Tca9554Wiring::kAddress, pin, levelHigh,
                                                   WaveshareAmoled18::Tca9554Wiring::kReleaseBusBeforeRead)
            && levelHigh;
    }

    bool primaryPressedRaw() {
        if constexpr (WaveshareAmoled18::Buttons::kBootPin < 0) {
            return false;
        }
        return !digitalRead(WaveshareAmoled18::Buttons::kBootPin);
    }

    bool powerPressedRaw() {
        return tcaPinHeld(WaveshareAmoled18::Tca9554Wiring::kPowerButtonPin);
    }

    void configureButtonPins() {
        if constexpr (WaveshareAmoled18::Buttons::kBootPin >= 0) {
            pinMode(WaveshareAmoled18::Buttons::kBootPin, INPUT_PULLUP);
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
        return {.debounceMs = WaveshareAmoled18::Buttons::kDebounceMs};
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
        return {WaveshareAmoled18::DisplayWiring::kPanelWidth, WaveshareAmoled18::DisplayWiring::kPanelHeight};
    }

    ::Input::TouchTiming touchTiming() {
        return {
            .failureBackoffMs = WaveshareAmoled18::TouchWiring::kFailureBackoffMs,
            .recoveryRetryMs = WaveshareAmoled18::TouchWiring::kRecoveryRetryMs,
            .recoveryEventIgnoreMs = WaveshareAmoled18::TouchWiring::kRecoveryEventIgnoreMs,
        };
    }

    bool beginTouch() {
        TwoWire& wire = touchWire();
        return Cst816Touch::probe(wire, WaveshareAmoled18::TouchWiring::kAddress)
            && Cst816Touch::configurePeriodicInterrupt(wire, WaveshareAmoled18::TouchWiring::kAddress);
    }

    bool touchReady() {
        if constexpr (WaveshareAmoled18::System::kTouchIrqPin < 0) {
            return true;
        }
        return !digitalRead(WaveshareAmoled18::System::kTouchIrqPin);
    }

    bool readTouch(ui::TouchContact& contact) {
        std::array<uint8_t, Cst816Touch::kPacketLength> data = {};
        if (!Cst816Touch::readPacket(touchWire(), WaveshareAmoled18::TouchWiring::kAddress,
                                     WaveshareAmoled18::TouchWiring::kReleaseBusBeforeRead, data.data(), data.size())) {
            return false;
        }

        BoardDrivers::Touch::Sample decoded = {};
        if (!Cst816Touch::decodePacket(data.data(), data.size(), WaveshareAmoled18::DisplayWiring::kPanelWidth,
                                       WaveshareAmoled18::DisplayWiring::kPanelHeight, decoded)) {
            return false;
        }

        contact = {decoded.touched, decoded.physicalX, decoded.physicalY};
        return true;
    }

} // namespace Board::Input
