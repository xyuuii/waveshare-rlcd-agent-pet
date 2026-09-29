#include "bridge_client.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string>
#include <string.h>

#ifdef ARDUINO
#include <HTTPClient.h>
#include <WiFi.h>
#endif

namespace {

bool isDisplayAscii(unsigned char ch) {
  return ch >= 32 && ch <= 126;
}

bool isAsciiAlphaNum(char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
         (ch >= 'a' && ch <= 'z');
}

const char* fallbackTaskForSource(SourceKind source) {
  switch (source) {
    case SourceKind::Codex:
      return "CODEX SESSION";
    case SourceKind::Hermes:
      return "HERMES SESSION";
    case SourceKind::OpenClaw:
      return "OPENCLAW SESSION";
    case SourceKind::ClaudeCode:
      return "CLAUDE SESSION";
    default:
      return "AGENT SESSION";
  }
}

std::string trimSpaces(const std::string& value) {
  size_t start = 0;
  while (start < value.size() && value[start] == ' ') {
    start += 1;
  }
  size_t end = value.size();
  while (end > start && value[end - 1] == ' ') {
    end -= 1;
  }
  return value.substr(start, end - start);
}

std::string sanitizeDisplayText(const char* value, SourceKind source) {
  if (!value || !value[0]) {
    return "";
  }

  std::string output;
  bool lastWasSpace = false;
  bool hasAlphaNum = false;
  bool sawNonAscii = false;
  for (const unsigned char ch : std::string(value)) {
    if (!isDisplayAscii(ch)) {
      sawNonAscii = true;
      if (!lastWasSpace && !output.empty()) {
        output.push_back(' ');
        lastWasSpace = true;
      }
      continue;
    }
    const char ascii = static_cast<char>(ch);
    if (ascii == ' ') {
      if (!lastWasSpace && !output.empty()) {
        output.push_back(' ');
        lastWasSpace = true;
      }
      continue;
    }
    output.push_back(ascii);
    lastWasSpace = false;
    hasAlphaNum = hasAlphaNum || isAsciiAlphaNum(ascii);
  }

  output = trimSpaces(output);
  if (!hasAlphaNum || (output.empty() && sawNonAscii)) {
    return fallbackTaskForSource(source);
  }
  return output;
}

}  // namespace

SourceKind parseSourceKind(const char* value) {
  if (!value) {
    return SourceKind::Unknown;
  }
  const std::string text(value);
  if (strcmp(value, "codex") == 0) {
    return SourceKind::Codex;
  }
  if (strcmp(value, "hermes") == 0) {
    return SourceKind::Hermes;
  }
  if (strcmp(value, "openclaw") == 0) {
    return SourceKind::OpenClaw;
  }
  if (strcmp(value, "claude-code") == 0) {
    return SourceKind::ClaudeCode;
  }
  if (text.find("codex") != std::string::npos) {
    return SourceKind::Codex;
  }
  if (text.find("hermes") != std::string::npos) {
    return SourceKind::Hermes;
  }
  if (text.find("openclaw") != std::string::npos) {
    return SourceKind::OpenClaw;
  }
  if (text.find("claude") != std::string::npos) {
    return SourceKind::ClaudeCode;
  }
  return SourceKind::Unknown;
}

AgentStatus parseAgentStatus(const char* value) {
  if (!value) {
    return AgentStatus::Idle;
  }
  if (strcmp(value, "running") == 0 || strcmp(value, "working") == 0 ||
      strcmp(value, "searching") == 0 || strcmp(value, "tool-use") == 0 ||
      strcmp(value, "tooluse") == 0 || strcmp(value, "tool_use") == 0 ||
      strcmp(value, "thinking") == 0 || strcmp(value, "planning") == 0 ||
      strcmp(value, "started") == 0 || strcmp(value, "tool-calling") == 0 ||
      strcmp(value, "awaiting-tool") == 0 || strcmp(value, "awaiting_tool") == 0 ||
      strcmp(value, "near-complete") == 0) {
    return AgentStatus::Running;
  }
  if (strcmp(value, "needs-attention") == 0 || strcmp(value, "blocked") == 0 ||
      strcmp(value, "awaiting-user") == 0 || strcmp(value, "awaiting_user") == 0) {
    return AgentStatus::NeedsAttention;
  }
  if (strcmp(value, "completed") == 0) {
    return AgentStatus::Completed;
  }
  if (strcmp(value, "error") == 0 || strcmp(value, "failed") == 0) {
    return AgentStatus::Error;
  }
  return AgentStatus::Idle;
}

const char* parseAgentStatusDetail(const char* value) {
  if (!value || !value[0]) {
    return "idle";
  }
  if (strcmp(value, "thinking") == 0 || strcmp(value, "planning") == 0) {
    return "thinking";
  }
  if (strcmp(value, "searching") == 0) {
    return "searching";
  }
  if (strcmp(value, "tool-use") == 0 || strcmp(value, "tooluse") == 0 ||
      strcmp(value, "tool_use") == 0) {
    return "tool-use";
  }
  if (strcmp(value, "working") == 0 || strcmp(value, "running") == 0 ||
      strcmp(value, "tool-calling") == 0 || strcmp(value, "awaiting-tool") == 0 ||
      strcmp(value, "awaiting_tool") == 0) {
    return "working";
  }
  if (strcmp(value, "started") == 0) {
    return "started";
  }
  if (strcmp(value, "near-complete") == 0) {
    return "almost-done";
  }
  if (strcmp(value, "needs-attention") == 0 || strcmp(value, "awaiting-user") == 0 ||
      strcmp(value, "awaiting_user") == 0 || strcmp(value, "blocked") == 0) {
    return "needs-attention";
  }
  if (strcmp(value, "completed") == 0) {
    return "completed";
  }
  if (strcmp(value, "error") == 0 || strcmp(value, "failed") == 0) {
    return "error";
  }
  return "idle";
}

namespace {

bool fillAgentState(const JsonDocument& doc, AgentState& outState) {
  JsonVariantConst notification = doc["notification"];

  const char* source = nullptr;
  const char* status = nullptr;
  const char* task = nullptr;
  const char* updatedAt = nullptr;
  const char* usageToday = doc["usage"]["today"].as<const char*>();
  const char* usageTodayLabel = doc["usage"]["today_label"].as<const char*>();
  const char* usageTodayHint = doc["usage"]["today_hint"].as<const char*>();
  const char* usageContext = doc["usage"]["context"].as<const char*>();
  const char* usageContextLabel = doc["usage"]["context_label"].as<const char*>();
  const char* usageContextHint = doc["usage"]["context_hint"].as<const char*>();
  const char* usageQuota = doc["usage"]["quota"].as<const char*>();
  const char* usageQuotaLabel = doc["usage"]["quota_label"].as<const char*>();
  const char* usageQuotaHint = doc["usage"]["quota_hint"].as<const char*>();
  const char* usageQuotaStyle = doc["usage"]["quota_style"].as<const char*>();

  const char* currentSource = doc["source"].as<const char*>();
  const char* currentStatus = doc["current_status"].as<const char*>();
  const char* currentTask = doc["task"].as<const char*>();
  const char* currentUpdatedAt = doc["updated_at"].as<const char*>();

  source = currentSource;
  status = currentStatus;
  task = currentTask;
  updatedAt = currentUpdatedAt;

  if ((!source || !source[0] || !status || !status[0] || strcmp(status, "event") == 0 ||
       !task || !task[0] || !updatedAt || !updatedAt[0]) && !notification.isNull()) {
    const char* notificationSource = notification["source"].as<const char*>();
    const char* notificationStatus = notification["status"].as<const char*>();
    const char* notificationTask = notification["task"].as<const char*>();
    const char* notificationUpdatedAt = notification["time"].as<const char*>();

    if ((!source || !source[0]) && notificationSource && notificationSource[0]) {
      source = notificationSource;
    }
    if ((!status || !status[0] || strcmp(status, "event") == 0) &&
        notificationStatus && notificationStatus[0] && strcmp(notificationStatus, "event") != 0) {
      status = notificationStatus;
    }
    if ((!task || !task[0])) {
      task = notificationTask;
      if (!task || !task[0]) {
        task = notification["message"].as<const char*>();
      }
    }
    if ((!updatedAt || !updatedAt[0]) && notificationUpdatedAt && notificationUpdatedAt[0]) {
      updatedAt = notificationUpdatedAt;
    }
  }

  if (!status || !status[0]) {
    status = doc["status"].as<const char*>();
  }

  const char* focusMode = doc["focus_mode"].as<const char*>();
  const char* focusId = doc["focus_id"].as<const char*>();

  outState.source = parseSourceKind(source);
  outState.status = parseAgentStatus(status);
  outState.statusDetail = parseAgentStatusDetail(status);
  outState.task = sanitizeDisplayText(task, outState.source);
  outState.updatedAt = std::string(updatedAt ? updatedAt : "");
  outState.focusMode =
      focusMode && strcmp(focusMode, "pinned") == 0 ? FocusMode::Pinned : FocusMode::Auto;
  outState.focusId = std::string(focusId ? focusId : "");
  outState.focusIndex = doc["focus_index"].isNull() ? -1 : doc["focus_index"].as<int>();
  const int rawFocusCount = doc["focus_count"].isNull() ? 0 : doc["focus_count"].as<int>();
  for (auto& slot : outState.agentSlots) {
    slot = AgentSlotSummary{};
  }
  JsonArrayConst slots = doc["agents"].as<JsonArrayConst>();
  int slotIndex = 0;
  for (JsonObjectConst slot : slots) {
    if (slotIndex >= kMaxAgentSlots) {
      break;
    }
    AgentSlotSummary& outSlot = outState.agentSlots[slotIndex];
    outSlot.id = std::string(slot["id"].as<const char*>() ? slot["id"].as<const char*>() : "");
    outSlot.source = parseSourceKind(slot["source"].as<const char*>());
    outSlot.status = parseAgentStatus(slot["status"].as<const char*>());
    outSlot.task = sanitizeDisplayText(slot["task"].as<const char*>(), outSlot.source);
    outSlot.updatedAt =
        std::string(slot["updated_at"].as<const char*>() ? slot["updated_at"].as<const char*>() : "");
    outSlot.present = true;
    slotIndex += 1;
  }
  outState.focusCount = rawFocusCount > 0 ? (rawFocusCount < slotIndex ? rawFocusCount : slotIndex) : slotIndex;
  if (outState.focusCount < 0) {
    outState.focusCount = 0;
  }
  if (outState.focusIndex < 0 || outState.focusIndex >= outState.focusCount) {
    outState.focusMode = FocusMode::Auto;
    outState.focusId.clear();
    outState.focusIndex = -1;
  }
  outState.usageToday = std::string(usageToday && usageToday[0] ? usageToday : "--");
  outState.usageTodayLabel = std::string(usageTodayLabel && usageTodayLabel[0] ? usageTodayLabel : "TODAY");
  outState.usageTodayHint = std::string(
      usageTodayHint && usageTodayHint[0] ? usageTodayHint : "Today total in this workspace");
  outState.usageContext = std::string(usageContext && usageContext[0] ? usageContext : "--");
  outState.usageContextLabel = std::string(
      usageContextLabel && usageContextLabel[0] ? usageContextLabel : "CONTEXT");
  outState.usageContextHint = std::string(
      usageContextHint && usageContextHint[0] ? usageContextHint : "Current turn tokens / model window");
  outState.usageQuota = std::string(usageQuota && usageQuota[0] ? usageQuota : "--");
  outState.usageQuotaLabel = std::string(usageQuotaLabel && usageQuotaLabel[0] ? usageQuotaLabel : "QUOTA");
  outState.usageQuotaHint = std::string(
      usageQuotaHint && usageQuotaHint[0] ? usageQuotaHint : "Remaining 5-hour and weekly limits");
  outState.usageQuotaStyle = std::string(usageQuotaStyle && usageQuotaStyle[0] ? usageQuotaStyle : "quota");
  outState.connected = true;
  return true;
}

uint32_t nonNegativeU32(JsonVariantConst value) {
  if (value.isNull()) {
    return 0;
  }
  const long long raw = value.as<long long>();
  if (raw < 0) {
    return 0;
  }
  return raw > 0xFFFFFFFFLL ? 0xFFFFFFFFu : static_cast<uint32_t>(raw);
}

void fillCommands(const JsonDocument& doc, BridgeCommands& commands) {
  commands = BridgeCommands{};
  commands.serverTimeMs = doc["server_time_ms"].isNull() ? 0 : doc["server_time_ms"].as<long long>();

  JsonVariantConst display = doc["display"];
  if (!display.isNull()) {
    BridgeDisplayCommand& out = commands.display;
    out.present = true;
    out.rev = nonNegativeU32(display["rev"]);
    ClockStyle style = ClockStyle::Sans;
    if (clockStyleFromName(display["clock_style"].as<const char*>(), style)) {
      out.hasStyle = true;
      out.style = style;
    }
    if (display["hour12"].is<bool>()) {
      out.hasHour12 = true;
      out.hour12 = display["hour12"].as<bool>();
    }
    if (display["show_seconds"].is<bool>()) {
      out.hasShowSeconds = true;
      out.showSeconds = display["show_seconds"].as<bool>();
    }
    ScreenPage page = ScreenPage::Overview;
    if (screenPageFromName(display["page"].as<const char*>(), page)) {
      out.hasPage = true;
      out.page = page;
    }
  }

  const char* tz = doc["time"]["tz"].as<const char*>();
  if (tz && tz[0] && strlen(tz) < 64) {
    commands.hasTz = true;
    commands.tz = tz;
  }

  JsonVariantConst egg = doc["egg"];
  if (!egg.isNull()) {
    const char* id = egg["id"].as<const char*>();
    const uint32_t frames = nonNegativeU32(egg["frames"]);
    const uint32_t fpsX100 = nonNegativeU32(egg["fps_x100"]);
    const uint32_t width = nonNegativeU32(egg["width"]);
    const uint32_t height = nonNegativeU32(egg["height"]);
    const bool sane = id && id[0] && strlen(id) <= 48 && frames > 0 && fpsX100 >= 100 && fpsX100 <= 6000 &&
                      width > 0 && width <= 400 && height > 0 && height <= 300;
    if (sane) {
      BridgeEggCommand& out = commands.egg;
      out.present = true;
      out.rev = nonNegativeU32(egg["rev"]);
      out.id = id;
      out.frames = frames;
      out.fpsX100 = static_cast<uint16_t>(fpsX100);
      out.width = static_cast<uint16_t>(width);
      out.height = static_cast<uint16_t>(height);
      out.startAtMs = egg["start_at_ms"].isNull() ? 0 : egg["start_at_ms"].as<long long>();
    }
  }
}

void appendParam(std::string& out, const char* key, const char* value) {
  out += '&';
  out += key;
  out += '=';
  for (const char* p = value; p && *p; ++p) {
    const char ch = *p;
    const bool safe = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                      ch == '-' || ch == '_' || ch == '.';
    if (safe) {
      out += ch;
    } else {
      static constexpr char kHex[] = "0123456789ABCDEF";
      out += '%';
      out += kHex[(static_cast<unsigned char>(ch) >> 4) & 0x0F];
      out += kHex[static_cast<unsigned char>(ch) & 0x0F];
    }
  }
}

void appendParam(std::string& out, const char* key, long value) {
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%ld", value);
  appendParam(out, key, buffer);
}

}  // namespace

bool parseAgentStatePayload(const char* json, AgentState& outState) {
  JsonDocument doc;
  if (!json || deserializeJson(doc, json) != DeserializationError::Ok) {
    return false;
  }
  return fillAgentState(doc, outState);
}

bool parsePollPayload(const char* json, AgentState& outState, BridgeCommands& commands) {
  JsonDocument doc;
  if (!json || deserializeJson(doc, json) != DeserializationError::Ok) {
    return false;
  }
  if (!fillAgentState(doc, outState)) {
    return false;
  }
  fillCommands(doc, commands);
  return true;
}

const char* screenPageName(ScreenPage page) {
  switch (page) {
    case ScreenPage::Usage:
      return "usage";
    case ScreenPage::Clock:
      return "clock";
    default:
      return "overview";
  }
}

bool screenPageFromName(const char* name, ScreenPage& out) {
  if (!name || !name[0]) {
    return false;
  }
  if (strcmp(name, "overview") == 0) {
    out = ScreenPage::Overview;
    return true;
  }
  if (strcmp(name, "usage") == 0) {
    out = ScreenPage::Usage;
    return true;
  }
  if (strcmp(name, "clock") == 0) {
    out = ScreenPage::Clock;
    return true;
  }
  return false;
}

std::string buildTelemetryQuery(const DeviceTelemetry& t) {
  std::string out;
  appendParam(out, "fw", t.firmware ? t.firmware : "");
  if (t.batteryValid) {
    appendParam(out, "bat", static_cast<long>(t.batteryPercent));
    appendParam(out, "mv", static_cast<long>(t.batteryMv));
  }
  if (t.charging) {
    appendParam(out, "chg", 1L);
  }
  if (t.climateValid) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%.1f", static_cast<double>(t.temperatureC));
    appendParam(out, "temp", buffer);
    snprintf(buffer, sizeof(buffer), "%.0f", static_cast<double>(t.humidityPct));
    appendParam(out, "hum", buffer);
  }
  if (t.rssi < 0) {
    appendParam(out, "rssi", static_cast<long>(t.rssi));
  }
  appendParam(out, "up", static_cast<long>(t.uptimeS));
  appendParam(out, "page", screenPageName(t.page));
  appendParam(out, "style", clockStyleName(t.clockStyle));
  appendParam(out, "srev", static_cast<long>(t.settingsRev));
  appendParam(out, "erev", static_cast<long>(t.eggRev));
  if (t.freeHeap > 0) {
    appendParam(out, "heap", static_cast<long>(t.freeHeap));
  }
  appendParam(out, "clk", t.timeValid ? 1L : 0L);
  return out;
}

bool fetchPollState(const char* url, AgentState& outState, BridgeCommands& commands, uint16_t timeoutMs) {
#ifdef ARDUINO
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  HTTPClient http;
  http.setConnectTimeout(timeoutMs);
  http.setTimeout(timeoutMs);
  if (!http.begin(url)) {
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  const String body = http.getString();
  http.end();
  return parsePollPayload(body.c_str(), outState, commands);
#else
  (void)url;
  (void)outState;
  (void)commands;
  (void)timeoutMs;
  return false;
#endif
}

bool fetchAgentState(const char* url, AgentState& outState) {
#ifdef ARDUINO
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  HTTPClient http;
  if (!http.begin(url)) {
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  const String body = http.getString();
  http.end();
  return parseAgentStatePayload(body.c_str(), outState);
#else
  (void)url;
  (void)outState;
  return false;
#endif
}
