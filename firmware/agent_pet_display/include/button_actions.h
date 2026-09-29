#pragma once

#include <stdint.h>

#include "models.h"

enum class ButtonUiAction {
  None,
  Page,
  AgentFocus,
  ClockStyle,  // next clock face (clock page)
  Egg,         // easter egg
};

struct ButtonGestureState {
  bool wasPressed{false};
  uint32_t pressedAtMs{0};
  bool longActionSent{false};
  bool veryLongActionSent{false};
};

// Short press fires on release; long press fires once while held and
// suppresses the release action.
ButtonUiAction updateButtonGesture(ButtonGestureState& state,
                                   bool pressed,
                                   uint32_t nowMs,
                                   uint32_t debounceMs,
                                   uint32_t longPressMs,
                                   ButtonUiAction shortAction,
                                   ButtonUiAction longAction);

// Same as above plus a third action after holding for veryLongPressMs.
ButtonUiAction updateButtonGestureEx(ButtonGestureState& state,
                                     bool pressed,
                                     uint32_t nowMs,
                                     uint32_t longPressMs,
                                     uint32_t veryLongPressMs,
                                     ButtonUiAction shortAction,
                                     ButtonUiAction longAction,
                                     ButtonUiAction veryLongAction);

// Two buttons held together. While both are down the single-button gestures
// are swallowed; fires once after holdMs.
struct ChordGestureState {
  bool active{false};
  uint32_t startedAtMs{0};
  bool fired{false};
};

bool updateChordGesture(ChordGestureState& chord,
                        ButtonGestureState& first,
                        ButtonGestureState& second,
                        bool firstPressed,
                        bool secondPressed,
                        uint32_t nowMs,
                        uint32_t holdMs);

// Agent actions become clock-style actions on the clock page.
ButtonUiAction contextualAction(ButtonUiAction action, ScreenPage page);
