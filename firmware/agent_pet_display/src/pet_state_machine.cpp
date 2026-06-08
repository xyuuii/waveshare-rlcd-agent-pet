#include "pet_state_machine.h"

#include <stdio.h>

namespace {

const char* sourceLabel(SourceKind source) {
  switch (source) {
    case SourceKind::Codex:
      return "CODEX";
    case SourceKind::Hermes:
      return "HERMES";
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

std::string tickerText(const std::string& statusDetail, const std::string& task, const std::string& updatedAt) {
  if (!task.empty()) {
    return statusDetail + "  " + task;
  }
  if (!updatedAt.empty()) {
    return updatedAt;
  }
  return statusDetail;
}

}  // namespace

DisplayState deriveDisplayState(const AgentState& agent,
                                const PowerState& power,
                                const EnvironmentState& environment,
                                ScreenPage page) {
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
  view.sidebarContext = agent.usageContext.empty() ? "--" : agent.usageContext;
  view.sidebarQuota = agent.usageQuota.empty() ? "--" : agent.usageQuota;
  view.buddyBubble = buddyBubbleText(agent.statusDetail, agent.status);
  view.footerMessage = agent.connected ? tickerText(view.statusDetail, agent.task, agent.updatedAt)
                                       : "Bridge offline";

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

  if (!agent.connected) {
    view.petMode = PetMode::Sleep;
    if (page == ScreenPage::Usage) {
      view.footerMessage = "PAGE 2/2 BOOT toggle";
    }
    return view;
  }
  if (power.lowBattery) {
    view.petMode = PetMode::Tired;
    if (page == ScreenPage::Usage) {
      view.footerMessage = "PAGE 2/2 BOOT toggle";
    }
    return view;
  }
  if (power.charging) {
    view.petMode = PetMode::Charging;
    if (page == ScreenPage::Usage) {
      view.footerMessage = "PAGE 2/2 BOOT toggle";
    }
    return view;
  }

  switch (agent.status) {
    case AgentStatus::Running:
      view.petMode = PetMode::Busy;
      break;
    case AgentStatus::NeedsAttention:
      view.petMode = PetMode::Attention;
      break;
    case AgentStatus::Completed:
      view.petMode = PetMode::Celebrate;
      break;
    case AgentStatus::Error:
      view.petMode = PetMode::Error;
      break;
    default:
      view.petMode = PetMode::Idle;
      break;
  }

  if (page == ScreenPage::Usage) {
    view.footerMessage = "PAGE 2/2 BOOT toggle";
  }

  return view;
}
