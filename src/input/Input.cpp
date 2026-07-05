#include "input/Input.h"

#include <algorithm>

#include "board/BoardImu.h"
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

struct TouchState {
  Board::UiOrientation orientation = Board::UiOrientation::Portrait;
  bool initialized = false;
  bool active = false;
  bool holdEmitted = false;
  uint8_t emptySamples = 0;
  uint8_t consecutiveReadFailures = 0;
  uint32_t startedAtMs = 0;
  uint32_t lastPollMs = 0;
  uint32_t backoffUntilMs = 0;
  uint32_t ignoreEventsUntilMs = 0;
  uint16_t startX = 0;
  uint16_t startY = 0;
  uint16_t lastX = 0;
  uint16_t lastY = 0;
};

struct TouchMotion {
  uint16_t dx = 0;
  uint16_t dy = 0;
  uint32_t durationMs = 0;
};

ControlsState gControls;
ControlTiming gControlTiming;
TouchSurface gTouchSurface;
TouchTiming gTouchTiming;
TouchState gTouch;

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

void resetTouchState() {
  gTouch.active = false;
  gTouch.holdEmitted = false;
  gTouch.emptySamples = 0;
  gTouch.startedAtMs = 0;
  gTouch.startX = 0;
  gTouch.startY = 0;
  gTouch.lastX = 0;
  gTouch.lastY = 0;
}

constexpr uint8_t orientationQuarterTurns(Board::UiOrientation orientation) {
  switch (orientation) {
    case Board::UiOrientation::Landscape:
      return 3;
    case Board::UiOrientation::LandscapeFlipped:
      return 1;
    case Board::UiOrientation::PortraitFlipped:
      return 2;
    case Board::UiOrientation::Portrait:
    default:
      return 0;
  }
}

TouchContact mapTouchContact(TouchContact contact) {
  const uint8_t quarterTurns = orientationQuarterTurns(gTouch.orientation);
  constexpr uint16_t kMin = 0;
  const uint16_t rawMaxX = std::max(gTouchSurface.width, uint16_t{1}) - 1;
  const uint16_t rawMaxY = std::max(gTouchSurface.height, uint16_t{1}) - 1;
  const uint16_t rawX = std::clamp(contact.x, kMin, rawMaxX);
  const uint16_t rawY = std::clamp(contact.y, kMin, rawMaxY);
  uint16_t x = rawX;
  uint16_t y = rawY;

  switch (quarterTurns) {
    case 1:
      x = rawMaxY - rawY;
      y = rawX;
      break;
    case 2:
      x = rawMaxX - rawX;
      y = rawMaxY - rawY;
      break;
    case 3:
      x = rawY;
      y = rawMaxX - rawX;
      break;
    default:
      break;
  }

  return {true, x, y};
}

TouchMotion touchMotion(uint32_t nowMs) {
  const auto delta = [](uint16_t left, uint16_t right) {
    uint16_t high = std::max(left, right);
    high -= std::min(left, right);
    return high;
  };

  return {delta(gTouch.lastX, gTouch.startX), delta(gTouch.lastY, gTouch.startY),
          nowMs - gTouch.startedAtMs};
}

bool touchReleaseIsTap(uint32_t nowMs) {
  const TouchMotion motion = touchMotion(nowMs);
  return motion.durationMs <= gTouchTiming.tapMaxDurationMs &&
         motion.dx <= gTouchTiming.tapMoveTolerancePx &&
         motion.dy <= gTouchTiming.tapMoveTolerancePx;
}

bool releaseTouch(uint32_t nowMs, Event &event) {
  if (!gTouch.active) {
    return false;
  }

  ++gTouch.emptySamples;
  if (gTouch.emptySamples < gTouchTiming.releaseConfirmSamples) {
    return false;
  }

  const bool tapped = touchReleaseIsTap(nowMs);
  gTouch.active = false;
  gTouch.emptySamples = 0;
  event = {};
  event.x = gTouch.lastX;
  event.y = gTouch.lastY;
  event.actions = ActionTouchRelease;
  if (tapped) {
    event.actions |= ActionTap;
  }
  return true;
}

bool pollTouchContact(const TouchContact &contact, uint32_t nowMs, Event &event) {
  if (!contact.touched) {
    return releaseTouch(nowMs, event);
  }

  gTouch.emptySamples = 0;

  const TouchContact mapped = mapTouchContact(contact);

  if (!gTouch.active) {
    gTouch.active = true;
    gTouch.holdEmitted = false;
    gTouch.startedAtMs = nowMs;
    gTouch.startX = mapped.x;
    gTouch.startY = mapped.y;
    gTouch.lastX = mapped.x;
    gTouch.lastY = mapped.y;
    event = {};
    event.x = mapped.x;
    event.y = mapped.y;
    event.actions = ActionTouchStart;
    return true;
  }

  gTouch.lastX = mapped.x;
  gTouch.lastY = mapped.y;
  event = {};
  event.x = mapped.x;
  event.y = mapped.y;
  event.actions = ActionTouchMove;
  const TouchMotion motion = touchMotion(nowMs);
  if (!gTouch.holdEmitted && motion.durationMs >= gTouchTiming.holdMs &&
      motion.dx <= gTouchTiming.tapMoveTolerancePx && motion.dy <= gTouchTiming.tapMoveTolerancePx) {
    gTouch.holdEmitted = true;
    event.actions |= ActionTouchHold;
  }
  return true;
}

void syncTouchOrientation() {
  const Board::UiOrientation orientation = Board::Imu::uiOrientation();
  if (gTouch.orientation == orientation) {
    return;
  }

  gTouch.orientation = orientation;
  resetTouchState();
}

bool beginTouch(uint32_t nowMs) {
  resetTouchState();
  gTouch.lastPollMs = 0;
  gTouch.backoffUntilMs = 0;
  gTouch.consecutiveReadFailures = 0;

  gTouch.initialized = Board::Input::beginTouch();
  if (gTouch.initialized) {
    gTouch.ignoreEventsUntilMs = nowMs + gTouchTiming.recoveryEventIgnoreMs;
  }
  return gTouch.initialized;
}

bool pollTouchEvent(uint32_t nowMs, Event &event) {
  syncTouchOrientation();

  {
    // Touch hardware can disappear during resets; retry without blocking button input.
    if (!gTouch.initialized) {
      if (nowMs >= gTouch.backoffUntilMs && !beginTouch(nowMs)) {
        gTouch.backoffUntilMs = nowMs + gTouchTiming.recoveryRetryMs;
      }
      return false;
    }
  }

  {
    // Keep failed reads from hammering the bus and keep normal polling bounded.
    if (nowMs < gTouch.backoffUntilMs ||
        nowMs - gTouch.lastPollMs < gTouchTiming.pollIntervalMs) {
      return false;
    }
    gTouch.lastPollMs = nowMs;
  }

  TouchContact contact;
  const bool contactRead = [&]() {
    if (!Board::Input::touchReady()) {
      contact = {};
      return true;
    }

    if (Board::Input::readTouch(contact)) {
      gTouch.consecutiveReadFailures = 0;
      return true;
    }

    gTouch.backoffUntilMs = nowMs + gTouchTiming.failureBackoffMs;
    ++gTouch.consecutiveReadFailures;

    if (gTouch.consecutiveReadFailures >= gTouchTiming.maxConsecutiveReadFailures) {
      gTouch.initialized = false;
      gTouch.backoffUntilMs = nowMs + gTouchTiming.recoveryRetryMs;
      resetTouchState();
    }
    return false;
  }();

  if (!contactRead) {
    return false;
  }

  if (nowMs < gTouch.ignoreEventsUntilMs) {
    resetTouchState();
    return false;
  }

  return pollTouchContact(contact, nowMs, event);
}

}  // namespace

bool begin() {
  if (!Board::Input::begin()) {
    return false;
  }

  const uint32_t nowMs = millis();
  gControlTiming = Board::Input::controlTiming();
  const PressActions actions = Board::Input::currentActions();
  resetControls(actions.shortPress, actions.longPress, nowMs);

  gTouchSurface = Board::Input::touchSurface();
  gTouchTiming = Board::Input::touchTiming();
  gTouch.orientation = Board::Imu::uiOrientation();
  beginTouch(nowMs);
  return true;
}

void end() {
  cancel();
  Board::Input::end();
}

void cancel() {
  const uint32_t nowMs = millis();
  const PressActions actions = Board::Input::currentActions();
  resetControls(actions.shortPress, actions.longPress, nowMs);
  resetTouchState();
  gTouch.initialized = false;
  Board::Input::cancel();
}

bool poll(Event &event, uint32_t nowMs) {
  event = {};

  const PressActions actions = Board::Input::currentActions();
  if (pollControlsEvent(actions.shortPress, actions.longPress, nowMs, event)) {
    return true;
  }

  return pollTouchEvent(nowMs, event);
}

}  // namespace Input
