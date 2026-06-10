#!/usr/bin/env node
import { fileURLToPath } from "node:url";

import { readStdinJson, relayBridgeEvent, toolStatusForName } from "./bridge-relay.js";

if (isMain()) {
  try {
    const input = await readStdinJson();
    const event = normalizeClaudeHookEvent(input);
    const result = await relayBridgeEvent(event);
    if (!result.ok) {
      console.error(`pet-claude-hook queued event after send failure: ${result.error}`);
    }
    process.exit(0);
  } catch (error) {
    // Hooks should be observational. Never block Claude Code because the pet is offline.
    console.error(`pet-claude-hook ignored error: ${error.message || String(error)}`);
    process.exit(0);
  }
}

export function normalizeClaudeHookEvent(input = {}) {
  return {
    ...input,
    source: input.source || "claude-code",
    type: input.hook_event_name || input.type || "hook",
    status: input.status || statusForHook(input.hook_event_name, input.tool_name || input.tool),
    message: input.message || messageForHook(input)
  };
}

function statusForHook(name, toolName) {
  if (name === "Notification") return "needs-attention";
  if (name === "Stop") return "completed";
  if (name === "SessionStart") return "started";
  if (name === "UserPromptSubmit") return "thinking";
  if (name === "PreToolUse" || name === "PostToolUse") return toolStatusForName(toolName);
  return "event";
}

function messageForHook(input) {
  if (input.notification?.message) return input.notification.message;
  if (input.tool_name) return `${input.hook_event_name}: ${input.tool_name}`;
  if (input.prompt) return "User prompt submitted";
  if (input.hook_event_name === "Stop") return "Claude Code task completed";
  return input.hook_event_name || "Claude Code event";
}

function isMain() {
  return process.argv[1] === fileURLToPath(import.meta.url);
}
