#include "focus_controller.h"

#include <stdint.h>

namespace {

bool hasExpired(uint32_t nowMs, uint32_t expiresAtMs) {
  return static_cast<int32_t>(nowMs - expiresAtMs) >= 0;
}

int normalizeVisibleCount(const AgentState& agent) {
  if (agent.focusCount <= 0) {
    return 0;
  }
  return agent.focusCount > kMaxAgentSlots ? kMaxAgentSlots : agent.focusCount;
}

bool isUsableSlot(const AgentState& agent, int index) {
  const int visibleCount = normalizeVisibleCount(agent);
  return index >= 0 && index < visibleCount && agent.agentSlots[index].present &&
         !agent.agentSlots[index].id.empty();
}

int findSlotIndexById(const AgentState& agent, const std::string& focusId) {
  if (focusId.empty()) {
    return -1;
  }
  const int visibleCount = normalizeVisibleCount(agent);
  for (int index = 0; index < visibleCount; ++index) {
    if (agent.agentSlots[index].present && agent.agentSlots[index].id == focusId) {
      return index;
    }
  }
  return -1;
}

void resetToAuto(FocusRequestState& state, int visibleCount) {
  state.mode = LocalFocusMode::Auto;
  state.focusId.clear();
  state.visibleIndex = -1;
  state.visibleCount = visibleCount;
  state.expiresAtMs = 0;
}

uint32_t resolveExpiresAt(const FocusRequestState& state, uint32_t nowMs, uint32_t autoReturnMs) {
  if (state.mode == LocalFocusMode::Pinned && !hasExpired(nowMs, state.expiresAtMs)) {
    return state.expiresAtMs;
  }
  return nowMs + autoReturnMs;
}

bool isUnreservedUrlChar(char value) {
  return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
         (value >= '0' && value <= '9') || value == '-' || value == '_' ||
         value == '.' || value == '~';
}

void appendUrlEncoded(std::string& url, const std::string& value) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  for (unsigned char ch : value) {
    if (isUnreservedUrlChar(static_cast<char>(ch))) {
      url.push_back(static_cast<char>(ch));
      continue;
    }
    url.push_back('%');
    url.push_back(kHex[(ch >> 4) & 0x0F]);
    url.push_back(kHex[ch & 0x0F]);
  }
}

}  // namespace

void advanceFocusSelection(FocusRequestState& state,
                           const AgentState& agent,
                           uint32_t nowMs,
                           uint32_t autoReturnMs) {
  const int visibleCount = normalizeVisibleCount(agent);
  if (visibleCount <= 0) {
    resetToAuto(state, 0);
    return;
  }

  int currentIndex = -1;
  if (state.mode == LocalFocusMode::Pinned) {
    currentIndex = state.visibleIndex;
    if (!isUsableSlot(agent, currentIndex) || agent.agentSlots[currentIndex].id != state.focusId) {
      currentIndex = findSlotIndexById(agent, state.focusId);
    }
  }

  const int nextIndex = currentIndex + 1;
  if (nextIndex < 0 || nextIndex >= visibleCount || !isUsableSlot(agent, nextIndex)) {
    resetToAuto(state, visibleCount);
    return;
  }

  state.mode = LocalFocusMode::Pinned;
  state.focusId = agent.agentSlots[nextIndex].id;
  state.visibleIndex = nextIndex;
  state.visibleCount = visibleCount;
  state.expiresAtMs = nowMs + autoReturnMs;
}

void expireFocusIfNeeded(FocusRequestState& state, uint32_t nowMs) {
  if (state.mode != LocalFocusMode::Pinned || !hasExpired(nowMs, state.expiresAtMs)) {
    return;
  }
  resetToAuto(state, state.visibleCount);
}

void syncFocusSelectionFromBridge(FocusRequestState& state,
                                  const AgentState& agent,
                                  uint32_t nowMs,
                                  uint32_t autoReturnMs) {
  const int visibleCount = normalizeVisibleCount(agent);
  if (visibleCount <= 0) {
    resetToAuto(state, 0);
    return;
  }

  if (agent.focusMode != FocusMode::Pinned || agent.focusId.empty()) {
    resetToAuto(state, visibleCount);
    return;
  }

  int resolvedIndex = agent.focusIndex;
  if (!isUsableSlot(agent, resolvedIndex) || agent.agentSlots[resolvedIndex].id != agent.focusId) {
    resolvedIndex = findSlotIndexById(agent, agent.focusId);
  }
  if (resolvedIndex < 0) {
    resetToAuto(state, visibleCount);
    return;
  }

  state.mode = LocalFocusMode::Pinned;
  state.focusId = agent.focusId;
  state.visibleIndex = resolvedIndex;
  state.visibleCount = visibleCount;
  state.expiresAtMs = resolveExpiresAt(state, nowMs, autoReturnMs);
}

std::string buildFocusedPollUrl(const char* baseUrl, const FocusRequestState& state) {
  std::string url(baseUrl ? baseUrl : "");
  if (url.find('?') == std::string::npos) {
    url.push_back('?');
  } else if (!url.empty() && url.back() != '?' && url.back() != '&') {
    url.push_back('&');
  }
  url += "focus=";
  if (state.mode == LocalFocusMode::Pinned && !state.focusId.empty()) {
    appendUrlEncoded(url, state.focusId);
  } else {
    url += "auto";
  }
  return url;
}
