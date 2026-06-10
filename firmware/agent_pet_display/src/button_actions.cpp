#include "button_actions.h"

namespace {

uint32_t elapsedSince(uint32_t nowMs, uint32_t thenMs) {
  return nowMs - thenMs;
}

void resetGesture(ButtonGestureState& state) {
  state.wasPressed = false;
  state.pressedAtMs = 0;
  state.longActionSent = false;
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
  if (pressed) {
    if (!state.wasPressed) {
      state.wasPressed = true;
      state.pressedAtMs = nowMs;
      state.longActionSent = false;
      return ButtonUiAction::None;
    }
    if (!state.longActionSent && elapsedSince(nowMs, state.pressedAtMs) >= longPressMs) {
      state.longActionSent = true;
      return longAction;
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
