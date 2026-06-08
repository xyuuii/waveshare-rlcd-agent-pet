#pragma once

#include <string>

enum class SourceKind {
  Codex,
  Hermes,
  ClaudeCode,
  Unknown,
};

enum class AgentStatus {
  Idle,
  Running,
  NeedsAttention,
  Completed,
  Error,
};

enum class PetMode {
  Sleep,
  Idle,
  Busy,
  Attention,
  Celebrate,
  Error,
  Tired,
  Charging,
};

enum class ScreenPage {
  Overview,
  Usage,
};

struct AgentState {
  SourceKind source{SourceKind::Unknown};
  AgentStatus status{AgentStatus::Idle};
  std::string statusDetail{"idle"};
  std::string task{};
  std::string updatedAt{};
  std::string usageToday{"--"};
  std::string usageContext{"--"};
  std::string usageQuota{"--"};
  bool connected{false};
};

struct PowerState {
  int voltageMv{0};
  int percent{0};
  bool charging{false};
  bool lowBattery{false};
  bool sampleOk{false};
};

struct EnvironmentState {
  bool clockValid{false};
  int year{0};
  int month{0};
  int day{0};
  int hour{0};
  int minute{0};
  int second{0};
  int weekday{0};
  float temperatureC{0.0f};
  float humidityPct{0.0f};
  bool climateValid{false};
};

struct DisplayState {
  ScreenPage page{ScreenPage::Overview};
  PetMode petMode{PetMode::Sleep};
  std::string sourceLabel{"UNKNOWN"};
  std::string taskLine{};
  std::string statusLabel{"idle"};
  std::string statusDetail{"IDLE"};
  bool offline{true};
  bool showBatteryDetail{false};
  std::string sidebarTime{"--:--:--"};
  std::string sidebarDate{"----/--/--"};
  std::string sidebarClimate{"--"};
  std::string sidebarAgent{"--"};
  std::string sidebarTokens{"--"};
  std::string sidebarContext{"--"};
  std::string sidebarQuota{"--"};
  std::string buddyBubble{"..."};
  std::string footerMessage{"--"};
};

inline bool shouldShowBatteryDetail(const PowerState& powerState) {
  return powerState.charging || powerState.lowBattery;
}

inline ScreenPage nextScreenPage(ScreenPage page) {
  return page == ScreenPage::Overview ? ScreenPage::Usage : ScreenPage::Overview;
}
