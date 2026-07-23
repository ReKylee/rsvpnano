#include "board/BoardInput.h"

#include <array>

#include <Wire.h>

#include "board/BoardPower.h"
#include "drivers/touch/ft6336/ft6336.h"
#include "platforms/waveshare_amoled_206/WaveshareAmoled206.h"

namespace {

    TwoWire& touchWire() {
        return Wire;
    }

    void resetTouchHardware() {
        if constexpr (WaveshareAmoled206::System::kTouchResetPin >= 0) {
            pinMode(WaveshareAmoled206::System::kTouchResetPin, OUTPUT);
            digitalWrite(WaveshareAmoled206::System::kTouchResetPin, LOW);
            delay(12);
            digitalWrite(WaveshareAmoled206::System::kTouchResetPin, HIGH);
            delay(12);
        }
    }

    bool primaryPressedRaw() {
        if constexpr (WaveshareAmoled206::Buttons::kBootPin < 0) {
            return false;
        }
        return !digitalRead(WaveshareAmoled206::Buttons::kBootPin);
    }

    bool powerPressedRaw() {
        return Board::Power::powerButtonHeld();
    }

    void configureButtonPins() {
        if constexpr (WaveshareAmoled206::Buttons::kBootPin >= 0) {
            pinMode(WaveshareAmoled206::Buttons::kBootPin, INPUT_PULLUP);
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
        return {WaveshareAmoled206::DisplayWiring::kPanelWidth, WaveshareAmoled206::DisplayWiring::kPanelHeight};
    }

    ::Input::TouchTiming touchTiming() {
        return {};
    }

    bool beginTouch() {
        resetTouchHardware();
        TwoWire& wire = touchWire();
        return Ft6336Touch::probe(wire, WaveshareAmoled206::TouchWiring::kAddress)
            && Ft6336Touch::configureMonitorMode(wire, WaveshareAmoled206::TouchWiring::kAddress);
    }

    bool touchReady() {
        if constexpr (WaveshareAmoled206::System::kTouchIrqPin < 0) {
            return true;
        }
        return !digitalRead(WaveshareAmoled206::System::kTouchIrqPin);
    }

    bool readTouch(ui::TouchContact& contact) {
        std::array<uint8_t, Ft6336Touch::kPacketLength> data = {};
        if (!Ft6336Touch::readPacket(touchWire(), WaveshareAmoled206::TouchWiring::kAddress,
                                     WaveshareAmoled206::TouchWiring::kReleaseBusBeforeRead, data.data(),
                                     data.size())) {
            return false;
        }

        BoardDrivers::Touch::Sample decoded = {};
        if (!Ft6336Touch::decodePacket(data.data(), data.size(), WaveshareAmoled206::DisplayWiring::kPanelWidth,
                                       WaveshareAmoled206::DisplayWiring::kPanelHeight, decoded)) {
            return false;
        }

        contact = {decoded.touched, decoded.physicalX, decoded.physicalY};
        return true;
    }

} // namespace Board::Input
