#include "board/BoardInput.h"

#include <array>

#include <Wire.h>

#include "board/BoardPower.h"
#include "drivers/touch/cst92xx/cst92xx.h"
#include "platforms/waveshare_amoled_216/WaveshareAmoled216.h"

namespace {

    TwoWire& touchWire() {
        return Wire;
    }

    void resetTouchHardware() {
        if constexpr (WaveshareAmoled216::System::kTouchResetPin >= 0) {
            pinMode(WaveshareAmoled216::System::kTouchResetPin, OUTPUT);
            digitalWrite(WaveshareAmoled216::System::kTouchResetPin, LOW);
            delay(12);
            digitalWrite(WaveshareAmoled216::System::kTouchResetPin, HIGH);
            delay(12);
        }
    }

    bool primaryPressedRaw() {
        if constexpr (WaveshareAmoled216::Buttons::kBootPin < 0) {
            return false;
        }
        return !digitalRead(WaveshareAmoled216::Buttons::kBootPin);
    }

    bool powerPressedRaw() {
        return Board::Power::powerButtonHeld();
    }

    bool keyPressedRaw() {
        if constexpr (WaveshareAmoled216::Buttons::kKeyPin < 0) {
            return false;
        }
        return !digitalRead(WaveshareAmoled216::Buttons::kKeyPin);
    }

    void configureButtonPins() {
        if constexpr (WaveshareAmoled216::Buttons::kBootPin >= 0) {
            pinMode(WaveshareAmoled216::Buttons::kBootPin, INPUT_PULLUP);
        }
        if constexpr (WaveshareAmoled216::Buttons::kKeyPin >= 0) {
            pinMode(WaveshareAmoled216::Buttons::kKeyPin, INPUT_PULLUP);
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
        const bool keyPressed = keyPressedRaw();
        if (primaryPressed) {
            actions.shortPress |= ::Input::ActionSelect | ::Input::ActionPlayPause;
            actions.longPress |= ::Input::ActionStandby;
        }
        if (powerPressed) {
            actions.shortPress |= ::Input::ActionOpenMenu | ::Input::ActionBack;
            actions.longPress |= ::Input::ActionPowerOff;
        }
        if (keyPressed) {
            actions.shortPress |= ::Input::ActionPlayPause;
        }
        return actions;
    }

    ui::TouchSurface touchSurface() {
        return {WaveshareAmoled216::DisplayWiring::kPanelWidth, WaveshareAmoled216::DisplayWiring::kPanelHeight};
    }

    ::Input::TouchTiming touchTiming() {
        return {};
    }

    bool beginTouch() {
        resetTouchHardware();
        TwoWire& wire = touchWire();
        return Cst92xxTouch::probe(wire, WaveshareAmoled216::TouchWiring::kAddress)
            && Cst92xxTouch::configureMonitorMode(wire, WaveshareAmoled216::TouchWiring::kAddress);
    }

    bool touchReady() {
        if constexpr (WaveshareAmoled216::System::kTouchIrqPin < 0) {
            return true;
        }
        return !digitalRead(WaveshareAmoled216::System::kTouchIrqPin);
    }

    bool readTouch(ui::TouchContact& contact) {
        std::array<uint8_t, Cst92xxTouch::kPacketLength> data = {};
        if (!Cst92xxTouch::readPacket(touchWire(), WaveshareAmoled216::TouchWiring::kAddress, data.data(),
                                      data.size())) {
            return false;
        }

        BoardDrivers::Touch::Sample decoded = {};
        if (!Cst92xxTouch::decodePacket(data.data(), data.size(), WaveshareAmoled216::DisplayWiring::kPanelWidth,
                                        WaveshareAmoled216::DisplayWiring::kPanelHeight, decoded)) {
            return false;
        }

        contact = {decoded.touched, decoded.physicalX, decoded.physicalY};
        return true;
    }

} // namespace Board::Input
