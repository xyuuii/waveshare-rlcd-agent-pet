#!/usr/bin/env node
import { fileURLToPath } from "node:url";

import {
  readStdinJson,
  relayBridgeEvent,
  stringValue,
  summarizeTask,
  toolStatusForName
} from "./bridge-relay.js";

const DEFAULT_SOURCE = process.env.PET_HERMES_SOURCE || "hermes";
const DEFAULT_TASK = process.env.PET_HERMES_TASK || "hermes-session";

if (isMain()) {
  try {
    const input = await readStdinJson();
    const event = normalizeHermesHookEvent(input);
    const result = await relayBridgeEvent(event);
    if (!result.ok) {
      console.error(`pet-hermes-hook queued event after send failure: ${result.error}`);
    }
    process.exit(0);
  } catch (error) {
    console.error(`pet-hermes-hook ignored error: ${error.message || String(error)}`);
    process.exit(0);
  }
}

export function normalizeHermesHookEvent(input = {}) {
  const extra = objectValue(input.extra);
  const hookEventName = stringValue(input.hook_event_name) || "event";
  const toolName = stringValue(input.tool_name) || stringValue(input.tool) || stringValue(extra.tool_name);
  const status = statusForHermesHook(hookEventName, toolName, extra);

  return {
    source: stringValue(input.source) || DEFAULT_SOURCE,
    type: hookEventName,
    status,
    message: messageForHermesHook(hookEventName, toolName, extra),
    task: taskForHermesHook(input, extra, toolName),
    workspace: stringValue(input.cwd) || stringValue(extra.cwd),
    sessionId: stringValue(input.session_id) || stringValue(extra.session_id),
    tool: toolName,
    notify: shouldNotify(status)
  };
}

function statusForHermesHook(hookEventName, toolName, extra) {
  switch (hookEventName) {
    case "pre_tool_call":
      return toolStatusForName(toolName);
    case "post_tool_call":
      return approvalPending(extra) ? "needs-attention" : "thinking";
    case "pre_llm_call":
      return "thinking";
    case "post_llm_call":
      return "completed";
    case "pre_approval_request":
      return "needs-attention";
    case "post_approval_response":
      return approvalDenied(extra) ? "needs-attention" : "thinking";
    case "on_session_start":
      return "started";
    case "on_session_end":
    case "on_session_finalize":
      return extra.completed === false && extra.interrupted === true ? "completed" : "completed";
    case "subagent_stop":
      return childFailed(extra) ? "error" : "thinking";
    default:
      return "event";
  }
}

function messageForHermesHook(hookEventName, toolName, extra) {
  switch (hookEventName) {
    case "pre_tool_call":
      return toolName ? `Hermes: ${toolName}` : "Hermes is using a tool";
    case "post_tool_call":
      if (approvalPending(extra)) return "Hermes is waiting on approval";
      return "Hermes is thinking";
    case "pre_llm_call":
      return "Hermes is thinking";
    case "post_llm_call":
      return "Hermes finished the turn";
    case "pre_approval_request":
      return toolName ? `Hermes needs approval for ${toolName}` : "Hermes needs approval";
    case "post_approval_response":
      return approvalDenied(extra) ? "Hermes approval was denied" : "Hermes resumed after approval";
    case "on_session_start":
      return "Hermes session started";
    case "on_session_end":
    case "on_session_finalize":
      return "Hermes session ended";
    case "subagent_stop":
      return childFailed(extra) ? "Hermes subagent failed" : "Hermes subagent returned";
    default:
      return "Hermes event";
  }
}

function taskForHermesHook(input, extra, toolName) {
  return summarizeTask(
    input.task,
    extra.user_message,
    input.prompt,
    extra.prompt,
    extra.task,
    extra.task_id,
    toolName,
    input.session_id
  ) || DEFAULT_TASK;
}

function objectValue(value) {
  return value && typeof value === "object" && !Array.isArray(value) ? value : {};
}

function childFailed(extra) {
  const status = stringValue(extra.child_status) || stringValue(extra.outcome);
  return ["error", "failed", "fail", "timeout", "killed"].includes(status);
}

function approvalPending(extra) {
  const status = stringValue(extra.status);
  return status === "approval-pending" || status === "needs-approval";
}

function approvalDenied(extra) {
  const choice = stringValue(extra.choice);
  const approved = typeof extra.approved === "boolean" ? extra.approved : null;
  if (approved === false) return true;
  return ["deny", "denied", "timeout", "cancelled"].includes(choice);
}

function shouldNotify(status) {
  return ["needs-attention", "completed", "error"].includes(status);
}

function isMain() {
  return process.argv[1] === fileURLToPath(import.meta.url);
}
