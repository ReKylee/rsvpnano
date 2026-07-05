#pragma once

#include <Arduino.h>

#include "board/BoardTypes.h"

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
  ActionTap = 1U << 6,
  ActionTouchStart = 1U << 7,
  ActionTouchMove = 1U << 8,
  ActionUp = 1U << 9,
  ActionDown = 1U << 10,
  ActionTouchHold = 1U << 11,
  ActionTouchRelease = 1U << 12,
};

struct Event {
  ActionMask actions = ActionNone;
  uint16_t x = 0;
  uint16_t y = 0;
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

struct TouchContact {
  bool touched = false;
  uint16_t x = 0;
  uint16_t y = 0;
};

struct TouchSurface {
  uint16_t width = 0;
  uint16_t height = 0;
};

struct TouchTiming {
  uint8_t releaseConfirmSamples = 2;
  uint8_t maxConsecutiveReadFailures = 5;
  uint16_t tapMoveTolerancePx = 20;
  uint16_t tapMaxDurationMs = 300;
  uint16_t holdMs = 420;
  uint32_t pollIntervalMs = 20;
  uint32_t failureBackoffMs = 250;
  uint32_t recoveryRetryMs = 1000;
  uint32_t recoveryEventIgnoreMs = 0;
};

constexpr bool isTouchEvent(const Event &event) {
  constexpr ActionMask touchActions = ActionTap | ActionTouchStart | ActionTouchMove | ActionTouchHold |
                                      ActionTouchRelease;
  return (event.actions & touchActions) != 0;
}

constexpr bool hasAction(ActionMask actions, ActionMask action) {
  return (actions & action) != 0;
}

bool begin();
void end();
void cancel();
bool poll(Event &event, uint32_t nowMs);

}  // namespace Input
