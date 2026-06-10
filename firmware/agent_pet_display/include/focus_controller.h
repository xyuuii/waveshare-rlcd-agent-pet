#pragma once

#include <stdint.h>

#include <string>

#include "models.h"

enum class LocalFocusMode {
  Auto,
  Pinned,
};

struct FocusRequestState {
  LocalFocusMode mode{LocalFocusMode::Auto};
  std::string focusId{};
  int visibleIndex{-1};
  int visibleCount{0};
  uint32_t expiresAtMs{0};
};

void advanceFocusSelection(FocusRequestState& state,
                           const AgentState& agent,
                           uint32_t nowMs,
                           uint32_t autoReturnMs);
void expireFocusIfNeeded(FocusRequestState& state, uint32_t nowMs);
void syncFocusSelectionFromBridge(FocusRequestState& state,
                                  const AgentState& agent,
                                  uint32_t nowMs,
                                  uint32_t autoReturnMs);
std::string buildFocusedPollUrl(const char* baseUrl, const FocusRequestState& state);
