#pragma once

#include <stdint.h>

enum class ButtonUiAction {
  None,
  Page,
  AgentFocus,
};

struct ButtonGestureState {
  bool wasPressed{false};
  uint32_t pressedAtMs{0};
  bool longActionSent{false};
};

ButtonUiAction updateButtonGesture(ButtonGestureState& state,
                                   bool pressed,
                                   uint32_t nowMs,
                                   uint32_t debounceMs,
                                   uint32_t longPressMs,
                                   ButtonUiAction shortAction,
                                   ButtonUiAction longAction);
