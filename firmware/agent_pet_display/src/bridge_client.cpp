#include "bridge_client.h"

#include <ArduinoJson.h>
#include <string>
#include <string.h>

#ifdef ARDUINO
#include <HTTPClient.h>
#include <WiFi.h>
#endif

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
  if (strcmp(value, "claude-code") == 0) {
    return SourceKind::ClaudeCode;
  }
  if (text.find("codex") != std::string::npos) {
    return SourceKind::Codex;
  }
  if (text.find("hermes") != std::string::npos) {
    return SourceKind::Hermes;
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
      strcmp(value, "started") == 0 ||
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
      strcmp(value, "tool-calling") == 0 || strcmp(value, "awaiting-tool") == 0) {
    return "working";
  }
  if (strcmp(value, "started") == 0) {
    return "started";
  }
  if (strcmp(value, "near-complete") == 0) {
    return "almost-done";
  }
  if (strcmp(value, "needs-attention") == 0 || strcmp(value, "awaiting-user") == 0 ||
      strcmp(value, "awaiting_user") == 0) {
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

bool parseAgentStatePayload(const char* json, AgentState& outState) {
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    return false;
  }

  JsonVariantConst notification = doc["notification"];

  const char* source = nullptr;
  const char* status = nullptr;
  const char* task = nullptr;
  const char* updatedAt = nullptr;
  const char* usageToday = doc["usage"]["today"].as<const char*>();
  const char* usageContext = doc["usage"]["context"].as<const char*>();
  const char* usageQuota = doc["usage"]["quota"].as<const char*>();

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

  outState.source = parseSourceKind(source);
  outState.status = parseAgentStatus(status);
  outState.statusDetail = parseAgentStatusDetail(status);
  outState.task = std::string(task ? task : "");
  outState.updatedAt = std::string(updatedAt ? updatedAt : "");
  outState.usageToday = std::string(usageToday && usageToday[0] ? usageToday : "--");
  outState.usageContext = std::string(usageContext && usageContext[0] ? usageContext : "--");
  outState.usageQuota = std::string(usageQuota && usageQuota[0] ? usageQuota : "--");
  outState.connected = true;
  return true;
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
