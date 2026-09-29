#include "pet_state_machine.h"

#include <stdio.h>

namespace {

constexpr const char* kFocusHint = "BOOT page  HOLD/KEY agent";

const char* sourceLabel(SourceKind source) {
  switch (source) {
    case SourceKind::Codex:
      return "CODEX";
    case SourceKind::Hermes:
      return "HERMES";
    case SourceKind::OpenClaw:
      return "OPENCLAW";
    case SourceKind::ClaudeCode:
      return "CLAUDE";
    default:
      return "UNKNOWN";
  }
}

const char* statusLabel(AgentStatus status) {
  switch (status) {
    case AgentStatus::Running:
      return "running";
    case AgentStatus::NeedsAttention:
      return "wait";
    case AgentStatus::Completed:
      return "done";
    case AgentStatus::Error:
      return "error";
    default:
      return "idle";
  }
}

const char* statusBadge(AgentStatus status) {
  switch (status) {
    case AgentStatus::Running:
      return "RUN";
    case AgentStatus::NeedsAttention:
      return "WAIT";
    case AgentStatus::Completed:
      return "DONE";
    case AgentStatus::Error:
      return "ERR";
    default:
      return "IDLE";
  }
}

const char* statusDetailLabel(const std::string& detail, AgentStatus status) {
  if (detail == "thinking") {
    return "THINKING";
  }
  if (detail == "searching") {
    return "SEARCHING";
  }
  if (detail == "tool-use") {
    return "TOOL USE";
  }
  if (detail == "working") {
    return "WORKING";
  }
  if (detail == "started") {
    return "STARTED";
  }
  if (detail == "almost-done") {
    return "ALMOST DONE";
  }
  if (detail == "needs-attention") {
    return "WAITING";
  }
  if (detail == "completed") {
    return "DONE";
  }
  if (detail == "error") {
    return "ERROR";
  }
  switch (status) {
    case AgentStatus::Running:
      return "WORKING";
    case AgentStatus::NeedsAttention:
      return "WAITING";
    case AgentStatus::Completed:
      return "DONE";
    case AgentStatus::Error:
      return "ERROR";
    default:
      return "IDLE";
  }
}

const char* statusDetailBadge(const std::string& detail, AgentStatus status) {
  if (detail == "thinking") {
    return "THNK";
  }
  if (detail == "searching") {
    return "SRCH";
  }
  if (detail == "tool-use") {
    return "TOOL";
  }
  if (detail == "working") {
    return "WORK";
  }
  if (detail == "started") {
    return "STRT";
  }
  if (detail == "almost-done") {
    return "ALMO";
  }
  switch (status) {
    case AgentStatus::NeedsAttention:
      return "WAIT";
    case AgentStatus::Completed:
      return "DONE";
    case AgentStatus::Error:
      return "ERR";
    case AgentStatus::Running:
      return "RUN";
    default:
      return "IDLE";
  }
}

const char* buddyBubbleText(const std::string& detail, AgentStatus status) {
  if (detail == "thinking") {
    return "thinking...";
  }
  if (detail == "searching") {
    return "searching...";
  }
  if (detail == "tool-use") {
    return "tool use";
  }
  if (detail == "working") {
    return "working...";
  }
  if (detail == "started") {
    return "starting...";
  }
  if (detail == "almost-done") {
    return "almost done";
  }
  if (detail == "needs-attention") {
    return "need input!";
  }
  if (detail == "completed") {
    return "done!";
  }
  if (detail == "error") {
    return "uh-oh";
  }
  switch (status) {
    case AgentStatus::Running:
      return "working...";
    case AgentStatus::NeedsAttention:
      return "need input!";
    case AgentStatus::Completed:
      return "done!";
    case AgentStatus::Error:
      return "uh-oh";
    default:
      return "idle";
  }
}

PetMode runningPetMode(const std::string& detail) {
  if (detail == "thinking") {
    return PetMode::Thinking;
  }
  if (detail == "searching") {
    return PetMode::Searching;
  }
  if (detail == "tool-use") {
    return PetMode::ToolUse;
  }
  if (detail == "started") {
    return PetMode::Starting;
  }
  if (detail == "almost-done") {
    return PetMode::AlmostDone;
  }
  if (detail == "working") {
    return PetMode::Working;
  }
  return PetMode::Working;
}

std::string tickerText(const std::string& statusDetail, const std::string& task, const std::string& updatedAt) {
  if (!task.empty()) {
    return statusDetail + "  " + task;
  }
  if (!updatedAt.empty()) {
    return updatedAt;
  }
  return statusDetail;
}

std::string focusLabelText(const AgentState& agent) {
  if (agent.focusMode == FocusMode::Pinned && agent.focusCount > 0 &&
      agent.focusIndex >= 0 && agent.focusIndex < agent.focusCount) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "PIN %d/%d", agent.focusIndex + 1, agent.focusCount);
    return buffer;
  }
  return "AUTO";
}

std::string wifiLinkLabel(const NetworkState& network) {
  if (!network.wifiKnown) {
    return "WIFI --";
  }
  if (network.wifiConnected) {
    if (network.rssi < 0) {
      char buffer[16];
      snprintf(buffer, sizeof(buffer), "WIFI %d", network.rssi);
      return buffer;
    }
    return "WIFI OK";
  }
  return network.wifiConnecting ? "WIFI TRY" : "WIFI OFF";
}

std::string networkLineText(const NetworkState& network) {
  if (!network.wifiKnown) {
    return "WIFI --";
  }
  if (network.wifiConnected) {
    if (network.rssi < 0) {
      char buffer[18];
      snprintf(buffer, sizeof(buffer), "WIFI %ddBm", network.rssi);
      return buffer;
    }
    return "WIFI OK";
  }
  char buffer[18];
  snprintf(buffer,
           sizeof(buffer),
           "%s %lu",
           network.wifiConnecting ? "WIFI TRY" : "WIFI OFF",
           static_cast<unsigned long>(network.reconnectAttempts));
  return buffer;
}

std::string bridgeLineText(const AgentState& agent, const NetworkState& network) {
  if (agent.connected) {
    return "BRIDGE OK";
  }
  if (network.wifiKnown && !network.wifiConnected) {
    return "BRIDGE WAIT";
  }
  return "BRIDGE OFF";
}

std::string offlineFooterText(const NetworkState& network) {
  if (network.wifiKnown && !network.wifiConnected) {
    return network.wifiConnecting ? "WiFi reconnecting" : "WiFi offline";
  }
  if (network.wifiConnected) {
    return "Bridge offline; WiFi OK";
  }
  return "Bridge offline";
}

PetMode derivePetMode(const AgentState& agent, const PowerState& power) {
  if (!agent.connected) {
    return PetMode::Sleep;
  }
  if (power.lowBattery) {
    return PetMode::Tired;
  }
  if (power.charging) {
    return PetMode::Charging;
  }
  switch (agent.status) {
    case AgentStatus::Running:
      return runningPetMode(agent.statusDetail);
    case AgentStatus::NeedsAttention:
      return PetMode::Attention;
    case AgentStatus::Completed:
      return PetMode::Celebrate;
    case AgentStatus::Error:
      return PetMode::Error;
    default:
      return PetMode::Idle;
  }
}

ClockView deriveClockView(const AgentState& agent,
                          const PowerState& power,
                          const EnvironmentState& environment,
                          const DisplaySettings& settings,
                          const DisplayState& view) {
  ClockView clock{};
  clock.style = settings.clockStyle;
  clock.hour12 = settings.hour12;
  clock.showSeconds = settings.showSeconds;
  clock.timeValid = environment.clockValid;
  if (environment.clockValid) {
    clock.year = environment.year;
    clock.month = environment.month;
    clock.day = environment.day;
    clock.weekday = environment.weekday;
    clock.hour = environment.hour;
    clock.minute = environment.minute;
    clock.second = environment.second;
  }
  clock.climateValid = environment.climateValid;
  clock.temperatureC = environment.temperatureC;
  clock.humidityPct = environment.humidityPct;
  clock.batteryValid = power.sampleOk;
  clock.batteryPercent = power.percent;
  clock.batteryLow = power.lowBattery;
  clock.charging = power.charging;
  clock.agentConnected = agent.connected;
  clock.agentSource = view.sourceLabel;
  clock.agentDetail = view.statusDetail;
  clock.agentTask = agent.task;
  clock.agentAttention = agent.connected && agent.status == AgentStatus::NeedsAttention;
  clock.agentActive = agent.connected && agent.status == AgentStatus::Running;
  clock.linkLabel = view.linkLabel;
  clock.bridgeLabel = view.bridgeLine;
  clock.petMode = view.petMode;
  clock.petBubble = agent.connected ? view.buddyBubble : std::string("zzz");

  int lines = 0;
  if (agent.connected) {
    const int visible = agent.focusCount < kMaxAgentSlots ? agent.focusCount : kMaxAgentSlots;
    for (int index = 0; index < visible && lines < kClockAgentLines; ++index) {
      const AgentSlotSummary& slot = agent.agentSlots[index];
      if (!slot.present) {
        continue;
      }
      ClockAgentLine& line = clock.agents[lines++];
      line.source = sourceLabel(slot.source);
      line.status = statusBadge(slot.status);
      line.attention = slot.status == AgentStatus::NeedsAttention;
      line.active = slot.status == AgentStatus::Running;
    }
    if (lines == 0) {
      ClockAgentLine& line = clock.agents[lines++];
      line.source = view.sourceLabel;
      line.status = statusDetailBadge(agent.statusDetail, agent.status);
      line.attention = clock.agentAttention;
      line.active = clock.agentActive;
    }
  }
  clock.agentCount = lines;

  // The pet face follows the day when nothing needs attention.
  const bool quiet = !clock.agentActive && !clock.agentAttention &&
                     (!agent.connected || agent.status == AgentStatus::Idle ||
                      agent.status == AgentStatus::Completed);
  if (clock.timeValid && quiet) {
    if (clock.hour >= 23 || clock.hour < 6) {
      clock.petMode = PetMode::Sleep;
      clock.petBubble = "zzz...";
    } else if (!agent.connected || agent.status == AgentStatus::Idle) {
      clock.petBubble = clock.hour < 12 ? "good morning!" : (clock.hour < 18 ? "good afternoon!" : "good evening!");
    }
  }
  return clock;
}

}  // namespace

DisplayState deriveDisplayState(const AgentState& agent,
                                const PowerState& power,
                                const EnvironmentState& environment,
                                ScreenPage page,
                                const NetworkState& network,
                                const DisplaySettings& settings) {
  DisplayState view{};
  view.page = page;
  view.sourceLabel = sourceLabel(agent.source);
  view.taskLine = agent.task.empty() ? "Waiting for next task" : agent.task;
  view.statusLabel = statusLabel(agent.status);
  view.statusDetail = statusDetailLabel(agent.statusDetail, agent.status);
  view.offline = !agent.connected;
  view.showBatteryDetail = shouldShowBatteryDetail(power);
  view.sidebarAgent = std::string(sourceLabel(agent.source)) + " " +
                      statusDetailBadge(agent.statusDetail, agent.status);
  view.sidebarTokens = agent.usageToday.empty() ? "--" : agent.usageToday;
  view.sidebarTokensLabel = agent.usageTodayLabel.empty() ? "TODAY" : agent.usageTodayLabel;
  view.sidebarTokensHint = agent.usageTodayHint.empty() ? "Today total in this workspace" : agent.usageTodayHint;
  view.sidebarContext = agent.usageContext.empty() ? "--" : agent.usageContext;
  view.sidebarContextLabel = agent.usageContextLabel.empty() ? "CONTEXT" : agent.usageContextLabel;
  view.sidebarContextHint =
      agent.usageContextHint.empty() ? "Current turn tokens / model window" : agent.usageContextHint;
  view.sidebarQuota = agent.usageQuota.empty() ? "--" : agent.usageQuota;
  view.sidebarQuotaLabel = agent.usageQuotaLabel.empty() ? "QUOTA" : agent.usageQuotaLabel;
  view.sidebarQuotaHint =
      agent.usageQuotaHint.empty() ? "Remaining 5-hour and weekly limits" : agent.usageQuotaHint;
  view.sidebarQuotaStyle = agent.usageQuotaStyle.empty() ? "quota" : agent.usageQuotaStyle;
  view.focusLabel = focusLabelText(agent);
  view.focusHint = kFocusHint;
  view.buddyBubble = buddyBubbleText(agent.statusDetail, agent.status);
  view.linkLabel = wifiLinkLabel(network);
  view.networkLine = networkLineText(network);
  view.bridgeLine = bridgeLineText(agent, network);
  view.footerMessage = agent.connected ? tickerText(view.statusDetail, agent.task, agent.updatedAt)
                                       : offlineFooterText(network);

  if (environment.clockValid) {
    char timeBuffer[12];
    char dateBuffer[16];
    snprintf(timeBuffer,
             sizeof(timeBuffer),
             "%02d:%02d:%02d",
             environment.hour,
             environment.minute,
             environment.second);
    snprintf(dateBuffer,
             sizeof(dateBuffer),
             "%04d/%02d/%02d",
             environment.year,
             environment.month,
             environment.day);
    view.sidebarTime = timeBuffer;
    view.sidebarDate = dateBuffer;
  }
  if (environment.climateValid) {
    char climateBuffer[20];
    snprintf(climateBuffer,
             sizeof(climateBuffer),
             "%.1fC %.0f%%",
             static_cast<double>(environment.temperatureC),
             static_cast<double>(environment.humidityPct));
    view.sidebarClimate = climateBuffer;
  }

  if (page == ScreenPage::Usage) {
    view.footerMessage = view.focusHint;
  }

  view.petMode = derivePetMode(agent, power);
  view.clock = deriveClockView(agent, power, environment, settings, view);
  return view;
}
