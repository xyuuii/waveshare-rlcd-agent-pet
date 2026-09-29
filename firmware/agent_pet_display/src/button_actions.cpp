#include "button_actions.h"

namespace {

uint32_t elapsedSince(uint32_t nowMs, uint32_t thenMs) {
  return nowMs - thenMs;
}

void resetGesture(ButtonGestureState& state) {
  state.wasPressed = false;
  state.pressedAtMs = 0;
  state.longActionSent = false;
  state.veryLongActionSent = false;
}

void swallowGesture(ButtonGestureState& state) {
  // Keep tracking the press but make sure neither long nor release actions fire.
  state.longActionSent = true;
  state.veryLongActionSent = true;
}

}  // namespace

ButtonUiAction updateButtonGesture(ButtonGestureState& state,
                                   bool pressed,
                                   uint32_t nowMs,
                                   uint32_t debounceMs,
                                   uint32_t longPressMs,
                                   ButtonUiAction shortAction,
                                   ButtonUiAction longAction) {
  (void)debounceMs;
  return updateButtonGestureEx(state, pressed, nowMs, longPressMs, 0, shortAction, longAction, ButtonUiAction::None);
}

ButtonUiAction updateButtonGestureEx(ButtonGestureState& state,
                                     bool pressed,
                                     uint32_t nowMs,
                                     uint32_t longPressMs,
                                     uint32_t veryLongPressMs,
                                     ButtonUiAction shortAction,
                                     ButtonUiAction longAction,
                                     ButtonUiAction veryLongAction) {
  if (pressed) {
    if (!state.wasPressed) {
      state.wasPressed = true;
      state.pressedAtMs = nowMs;
      state.longActionSent = false;
      state.veryLongActionSent = false;
      return ButtonUiAction::None;
    }
    const uint32_t held = elapsedSince(nowMs, state.pressedAtMs);
    if (!state.longActionSent && held >= longPressMs) {
      state.longActionSent = true;
      return longAction;
    }
    if (veryLongPressMs > longPressMs && veryLongAction != ButtonUiAction::None && state.longActionSent &&
        !state.veryLongActionSent && held >= veryLongPressMs) {
      state.veryLongActionSent = true;
      return veryLongAction;
    }
    return ButtonUiAction::None;
  }

  if (!state.wasPressed) {
    return ButtonUiAction::None;
  }

  const bool longActionSent = state.longActionSent;
  resetGesture(state);
  if (longActionSent) {
    return ButtonUiAction::None;
  }
  return shortAction;
}

bool updateChordGesture(ChordGestureState& chord,
                        ButtonGestureState& first,
                        ButtonGestureState& second,
                        bool firstPressed,
                        bool secondPressed,
                        uint32_t nowMs,
                        uint32_t holdMs) {
  if (firstPressed && secondPressed) {
    if (!chord.active) {
      chord.active = true;
      chord.startedAtMs = nowMs;
      chord.fired = false;
    }
    swallowGesture(first);
    swallowGesture(second);
    if (!chord.fired && elapsedSince(nowMs, chord.startedAtMs) >= holdMs) {
      chord.fired = true;
      return true;
    }
    return false;
  }
  if (chord.active) {
    // One button released: the other one must not fire a stray short press.
    swallowGesture(first);
    swallowGesture(second);
  }
  if (!firstPressed && !secondPressed) {
    chord = ChordGestureState{};
  }
  return false;
}

ButtonUiAction contextualAction(ButtonUiAction action, ScreenPage page) {
  if (page == ScreenPage::Clock && action == ButtonUiAction::AgentFocus) {
    return ButtonUiAction::ClockStyle;
  }
  return action;
}
