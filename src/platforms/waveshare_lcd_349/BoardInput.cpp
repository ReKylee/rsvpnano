#include "board/BoardInput.h"

#include <array>

#include <Wire.h>

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
        return {WaveshareLcd349::Buttons::kDebounceMs, WaveshareLcd349::Buttons::kShortPressMaxMs,
                WaveshareLcd349::Buttons::kLongPressMs};
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

    ui::TouchTiming touchTiming() {
        ui::TouchTiming timing = {};
        timing.releaseConfirmSamples = WaveshareLcd349::TouchWiring::kReleaseConfirmSamples;
        timing.maxConsecutiveReadFailures = WaveshareLcd349::TouchWiring::kMaxConsecutiveReadFailures;
        timing.pollIntervalMs = WaveshareLcd349::TouchWiring::kPollIntervalMs;
        timing.failureBackoffMs = WaveshareLcd349::TouchWiring::kFailureBackoffMs;
        timing.recoveryRetryMs = WaveshareLcd349::TouchWiring::kRecoveryRetryMs;
        timing.recoveryEventIgnoreMs = WaveshareLcd349::TouchWiring::kRecoveryEventIgnoreMs;
        return timing;
    }

    bool beginTouch() {
        return Axs15231bTouch::probe(touchWire(), WaveshareLcd349::TouchWiring::kAddress);
    }

    bool touchReady() {
        bool high = true;
        if (!BoardDrivers::Tca9554::readInputPin(Wire1, WaveshareLcd349::Tca9554Wiring::kAddress,
                                                 WaveshareLcd349::Tca9554Wiring::kTouchInterruptPin, high,
                                                 WaveshareLcd349::Tca9554Wiring::kReleaseBusBeforeRead)) {
            return false;
        }
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
