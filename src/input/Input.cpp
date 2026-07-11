#include "input/Input.h"

#include "board/BoardInput.h"

namespace Input {
namespace {

struct ControlsState {
  bool initialized = false;
  ActionMask stableShortActions = ActionNone;
  ActionMask stableLongActions = ActionNone;
  ActionMask candidateShortActions = ActionNone;
  ActionMask candidateLongActions = ActionNone;
  ActionMask activeShortActions = ActionNone;
  ActionMask activeLongActions = ActionNone;
  bool releasedEvent = false;
  uint32_t candidateSinceMs = 0;
  uint32_t pressStartedMs = 0;
  uint32_t lastPressDurationMs = 0;
};

ControlsState gControls;
ControlTiming gControlTiming;

bool anyAction(ActionMask shortActions, ActionMask longActions) {
  return shortActions != ActionNone || longActions != ActionNone;
}

void resetControls(ActionMask shortActions, ActionMask longActions, uint32_t nowMs) {
  gControls.initialized = true;
  gControls.stableShortActions = shortActions;
  gControls.stableLongActions = longActions;
  gControls.candidateShortActions = shortActions;
  gControls.candidateLongActions = longActions;
  gControls.activeShortActions = ActionNone;
  gControls.activeLongActions = ActionNone;
  gControls.releasedEvent = false;
  gControls.candidateSinceMs = nowMs;
  gControls.pressStartedMs = anyAction(shortActions, longActions) ? nowMs : 0;
  gControls.lastPressDurationMs = 0;
}

void updateControls(ActionMask shortActions, ActionMask longActions, uint32_t nowMs) {
  if (!gControls.initialized) {
    resetControls(shortActions, longActions, nowMs);
    return;
  }

  gControls.releasedEvent = false;

  {
    // Debounce the raw bitmask before changing the stable pressed controls.
    if (shortActions != gControls.candidateShortActions || longActions != gControls.candidateLongActions) {
      gControls.candidateShortActions = shortActions;
      gControls.candidateLongActions = longActions;
      gControls.candidateSinceMs = nowMs;
    }

    if (gControls.candidateShortActions == gControls.stableShortActions &&
        gControls.candidateLongActions == gControls.stableLongActions) {
      return;
    }

    if (nowMs - gControls.candidateSinceMs < gControlTiming.debounceMs) {
      return;
    }
  }

  const ActionMask previousShortActions = gControls.stableShortActions;
  const ActionMask previousLongActions = gControls.stableLongActions;
  gControls.stableShortActions = gControls.candidateShortActions;
  gControls.stableLongActions = gControls.candidateLongActions;

  {
    // Track the gesture lifetime for whichever controls became active together.
    if (!anyAction(previousShortActions, previousLongActions) &&
        anyAction(gControls.stableShortActions, gControls.stableLongActions)) {
      gControls.activeShortActions = gControls.stableShortActions;
      gControls.activeLongActions = gControls.stableLongActions;
      gControls.pressStartedMs = nowMs;
      gControls.lastPressDurationMs = 0;
      return;
    }

    if (anyAction(previousShortActions, previousLongActions) &&
        !anyAction(gControls.stableShortActions, gControls.stableLongActions)) {
      gControls.releasedEvent = true;
      gControls.lastPressDurationMs = nowMs - gControls.pressStartedMs;
      return;
    }

    if (previousShortActions != gControls.stableShortActions ||
        previousLongActions != gControls.stableLongActions) {
      gControls.activeShortActions = gControls.stableShortActions;
      gControls.activeLongActions = gControls.stableLongActions;
      gControls.pressStartedMs = nowMs;
      gControls.lastPressDurationMs = 0;
    }
  }
}

bool pollControlsEvent(ActionMask shortActions, ActionMask longActions, uint32_t nowMs, Event &event) {
  updateControls(shortActions, longActions, nowMs);

  if (anyAction(gControls.stableShortActions, gControls.stableLongActions) &&
      anyAction(gControls.activeShortActions, gControls.activeLongActions) &&
      nowMs - gControls.pressStartedMs >= gControlTiming.longPressMs) {
    const ActionMask actions = gControls.activeLongActions;
    if (actions == ActionNone) {
      gControls.activeShortActions = ActionNone;
      gControls.activeLongActions = ActionNone;
      return false;
    }
    event = {};
    event.actions = actions;
    gControls.activeShortActions = ActionNone;
    gControls.activeLongActions = ActionNone;
    return true;
  }

  if (gControls.releasedEvent && anyAction(gControls.activeShortActions, gControls.activeLongActions) &&
      gControls.lastPressDurationMs <= gControlTiming.shortPressMaxMs) {
    const ActionMask actions = gControls.activeShortActions;
    if (actions == ActionNone) {
      gControls.activeShortActions = ActionNone;
      gControls.activeLongActions = ActionNone;
      return false;
    }
    event = {};
    event.actions = actions;
    gControls.activeShortActions = ActionNone;
    gControls.activeLongActions = ActionNone;
    return true;
  }

  if (gControls.releasedEvent) {
    gControls.activeShortActions = ActionNone;
    gControls.activeLongActions = ActionNone;
  }
  return false;
}

}  // namespace

bool begin() {
  gControls.initialized = false;
  if (!Board::Input::begin()) {
    return false;
  }

  gControlTiming = Board::Input::controlTiming();
  const PressActions actions = Board::Input::currentActions();
  resetControls(actions.shortPress, actions.longPress, millis());
  return true;
}

void end() {
  cancel();
  Board::Input::end();
  gControls.initialized = false;
}

void cancel() {
  const uint32_t nowMs = millis();
  const PressActions actions = Board::Input::currentActions();
  resetControls(actions.shortPress, actions.longPress, nowMs);
  Board::Input::cancel();
}

bool poll(Event &event, uint32_t nowMs) {
  event = {};
  if (!gControls.initialized) {
    return false;
  }

  const PressActions actions = Board::Input::currentActions();
  return pollControlsEvent(actions.shortPress, actions.longPress, nowMs, event);
}

}  // namespace Input
