#pragma once

#include <Arduino.h>

namespace Input {

    using ActionMask = uint16_t;

    enum Action : ActionMask {
        ActionNone = 0,
        ActionSelect = 1U << 0,
        ActionBack = 1U << 1,
        ActionOpenMenu = 1U << 2,
        ActionPlayPause = 1U << 3,
        ActionStandby = 1U << 4,
        ActionPowerOff = 1U << 5,
    };

    struct Event {
        ActionMask actions = ActionNone;
    };

    struct PressActions {
        ActionMask shortPress = ActionNone;
        ActionMask longPress = ActionNone;
    };

    struct ControlTiming {
        uint16_t debounceMs = 25;
        uint16_t shortPressMaxMs = 700;
        uint16_t longPressMs = 900;
    };

    constexpr bool hasAction(ActionMask actions, ActionMask action) {
        return (actions & action) != 0;
    }

    bool begin();
    void end();
    void cancel();
    bool poll(Event& event, uint32_t nowMs);

} // namespace Input
