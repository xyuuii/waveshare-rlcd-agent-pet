#pragma once

#include "models.h"

bool parseAgentStatePayload(const char* json, AgentState& outState);
SourceKind parseSourceKind(const char* value);
AgentStatus parseAgentStatus(const char* value);
bool fetchAgentState(const char* url, AgentState& outState);
