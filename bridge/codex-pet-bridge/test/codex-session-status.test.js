import test from "node:test";
import assert from "node:assert/strict";

import { inferCodexRealtimeStateFromLines, pickCurrentState } from "../src/codex-session-status.js";

function line(value) {
  return JSON.stringify(value);
}

test("reasoning after a user turn maps to thinking", () => {
  const state = inferCodexRealtimeStateFromLines([
    line({ timestamp: "2026-06-08T08:20:00.000Z", type: "event_msg", payload: { type: "task_started" } }),
    line({ timestamp: "2026-06-08T08:20:01.000Z", type: "event_msg", payload: { type: "user_message", message: "help" } }),
    line({ timestamp: "2026-06-08T08:20:02.000Z", type: "response_item", payload: { type: "reasoning" } })
  ]);

  assert.equal(state?.status, "thinking");
  assert.equal(state?.time, "2026-06-08T08:20:02.000Z");
});

test("web search activity maps to searching", () => {
  const state = inferCodexRealtimeStateFromLines([
    line({ timestamp: "2026-06-08T08:21:00.000Z", type: "response_item", payload: { type: "reasoning" } }),
    line({ timestamp: "2026-06-08T08:21:01.000Z", type: "response_item", payload: { type: "web_search_call" } })
  ]);

  assert.equal(state?.status, "searching");
  assert.equal(state?.time, "2026-06-08T08:21:01.000Z");
});

test("generic function calls map to tool-use", () => {
  const state = inferCodexRealtimeStateFromLines([
    line({ timestamp: "2026-06-08T08:22:00.000Z", type: "response_item", payload: { type: "function_call", name: "exec_command" } })
  ]);

  assert.equal(state?.status, "tool-use");
  assert.equal(state?.time, "2026-06-08T08:22:00.000Z");
});

test("function call output falls back to thinking while Codex processes results", () => {
  const state = inferCodexRealtimeStateFromLines([
    line({ timestamp: "2026-06-08T08:23:00.000Z", type: "response_item", payload: { type: "function_call_output" } })
  ]);

  assert.equal(state?.status, "thinking");
});

test("task completion maps to completed", () => {
  const state = inferCodexRealtimeStateFromLines([
    line({ timestamp: "2026-06-08T08:24:00.000Z", type: "event_msg", payload: { type: "agent_message", message: "done" } }),
    line({ timestamp: "2026-06-08T08:24:01.000Z", type: "event_msg", payload: { type: "task_complete" } })
  ]);

  assert.equal(state?.status, "completed");
  assert.equal(state?.time, "2026-06-08T08:24:01.000Z");
});

test("turn_aborted maps to completed", () => {
  const state = inferCodexRealtimeStateFromLines([
    line({ timestamp: "2026-06-08T08:25:00.000Z", type: "response_item", payload: { type: "reasoning" } }),
    line({ timestamp: "2026-06-08T08:25:01.000Z", type: "event_msg", payload: { type: "turn_aborted", reason: "interrupted" } })
  ]);

  assert.equal(state?.status, "completed");
  assert.equal(state?.time, "2026-06-08T08:25:01.000Z");
});

test("agent_message indicates Codex is still processing (maps to thinking)", () => {
  const state = inferCodexRealtimeStateFromLines([
    line({ timestamp: "2026-06-08T08:26:00.000Z", type: "response_item", payload: { type: "function_call_output" } }),
    line({ timestamp: "2026-06-08T08:26:01.000Z", type: "event_msg", payload: { type: "agent_message", message: "checking status…" } })
  ]);

  assert.equal(state?.status, "thinking");
  assert.equal(state?.time, "2026-06-08T08:26:01.000Z");
});

test("assistant message maps to thinking (Codex generating response)", () => {
  const state = inferCodexRealtimeStateFromLines([
    line({ timestamp: "2026-06-08T08:27:00.000Z", type: "response_item", payload: { type: "reasoning" } }),
    line({ timestamp: "2026-06-08T08:27:01.000Z", type: "response_item", payload: { type: "message", role: "assistant", content: [{ type: "output_text", text: "here is the result" }] } })
  ]);

  assert.equal(state?.status, "thinking");
  assert.equal(state?.time, "2026-06-08T08:27:01.000Z");
});

test("user input request maps to needs-attention", () => {
  const state = inferCodexRealtimeStateFromLines([
    line({ timestamp: "2026-06-08T08:28:00.000Z", type: "response_item", payload: { type: "function_call", name: "exec_command" } }),
    line({ timestamp: "2026-06-08T08:28:01.000Z", type: "response_item", payload: { type: "function_call", name: "request_user_input" } })
  ]);

  assert.equal(state?.status, "needs-attention");
  assert.equal(state?.time, "2026-06-08T08:28:01.000Z");
});

// ── pickCurrentState ──────────────────────────────────────────

test("pickCurrentState prefers codexRealtime when timestamps tie", () => {
  const event = { source: "laptop-codex", task: "runtime", status: "running", time: "2026-06-08T10:00:00.000Z" };
  const realtime = { source: "laptop-codex", task: "runtime", status: "tool-use", time: "2026-06-08T10:00:00.000Z" };

  const result = pickCurrentState(event, realtime);
  assert.equal(result?.status, "tool-use", "realtime finer-grained status wins on tie");
});

test("pickCurrentState prefers codexRealtime when more recent", () => {
  const event = { source: "laptop-codex", task: "runtime", status: "running", time: "2026-06-08T10:00:00.000Z" };
  const realtime = { source: "laptop-codex", task: "runtime", status: "searching", time: "2026-06-08T10:00:05.000Z" };

  const result = pickCurrentState(event, realtime);
  assert.equal(result?.status, "searching", "realtime wins when fresher");
});

test("pickCurrentState falls back to event when realtime is older", () => {
  const event = { source: "laptop-codex", task: "runtime", status: "completed", time: "2026-06-08T10:00:10.000Z" };
  const realtime = { source: "laptop-codex", task: "runtime", status: "tool-use", time: "2026-06-08T10:00:00.000Z" };

  const result = pickCurrentState(event, realtime);
  assert.equal(result?.status, "completed", "event wins when fresher");
});

test("pickCurrentState keeps a non-codex source active even if codex realtime is newer", () => {
  const event = { source: "hermes", task: "review docs", status: "thinking", time: "2026-06-08T10:00:00.000Z" };
  const realtime = { source: "laptop-codex", task: "runtime", status: "tool-use", time: "2026-06-08T10:00:05.000Z" };

  const result = pickCurrentState(event, realtime);
  assert.equal(result?.source, "hermes");
  assert.equal(result?.status, "thinking");
});

test("pickCurrentState returns event when realtime is null", () => {
  const event = { source: "laptop-codex", task: "runtime", status: "running", time: "2026-06-08T10:00:00.000Z" };

  const result = pickCurrentState(event, null);
  assert.equal(result?.status, "running");
});

test("pickCurrentState returns realtime when event is null", () => {
  const realtime = { source: "laptop-codex", task: "runtime", status: "thinking", time: "2026-06-08T10:00:00.000Z" };

  const result = pickCurrentState(null, realtime);
  assert.equal(result?.status, "thinking");
});

test("pickCurrentState returns null when both are null", () => {
  assert.equal(pickCurrentState(null, null), null);
});
