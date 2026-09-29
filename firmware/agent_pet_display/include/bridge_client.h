#pragma once

#include <stdint.h>

#include <string>

#include "models.h"

// Optional instructions the bridge can piggyback on /esp32/poll.
struct BridgeDisplayCommand {
  bool present{false};
  uint32_t rev{0};
  bool hasStyle{false};
  ClockStyle style{ClockStyle::Sans};
  bool hasHour12{false};
  bool hour12{false};
  bool hasShowSeconds{false};
  bool showSeconds{true};
  bool hasPage{false};
  ScreenPage page{ScreenPage::Overview};
};

struct BridgeEggCommand {
  bool present{false};
  uint32_t rev{0};
  std::string id{};
  uint32_t frames{0};
  uint16_t fpsX100{0};
  uint16_t width{0};
  uint16_t height{0};
  int64_t startAtMs{0};
};

struct BridgeCommands {
  int64_t serverTimeMs{0};
  BridgeDisplayCommand display{};
  bool hasTz{false};
  std::string tz{};
  BridgeEggCommand egg{};
};

// What the board reports about itself on every poll (query string).
struct DeviceTelemetry {
  const char* firmware{""};
  bool batteryValid{false};
  int batteryPercent{0};
  int batteryMv{0};
  bool charging{false};
  bool climateValid{false};
  float temperatureC{0.0f};
  float humidityPct{0.0f};
  int rssi{0};
  uint32_t uptimeS{0};
  ScreenPage page{ScreenPage::Overview};
  ClockStyle clockStyle{ClockStyle::Sans};
  uint32_t settingsRev{0};
  uint32_t eggRev{0};
  uint32_t freeHeap{0};
  bool timeValid{false};
  uint32_t eggRequest{0};  // bumps when the secret gesture asks the bridge for its easter egg
};

bool parseAgentStatePayload(const char* json, AgentState& outState);
bool parsePollPayload(const char* json, AgentState& outState, BridgeCommands& commands);
SourceKind parseSourceKind(const char* value);
AgentStatus parseAgentStatus(const char* value);
const char* screenPageName(ScreenPage page);
bool screenPageFromName(const char* name, ScreenPage& out);
// "&fw=1.3.0&bat=84&..." (leading '&', values URL-safe).
std::string buildTelemetryQuery(const DeviceTelemetry& telemetry);
// ".../esp32/poll?token=x" -> ".../esp32/anim/<id>/frames?token=x&start=..&count=..&max_bytes=.."
std::string buildAnimChunkUrl(const char* pollUrl, const std::string& id, uint32_t start, uint32_t count,
                              uint32_t maxBytes);
// Hides the token query value for logs.
std::string redactUrlToken(const char* url);

bool fetchAgentState(const char* url, AgentState& outState);
// Fetch with explicit timeouts and command parsing; used by the network task.
bool fetchPollState(const char* url, AgentState& outState, BridgeCommands& commands, uint16_t timeoutMs);
