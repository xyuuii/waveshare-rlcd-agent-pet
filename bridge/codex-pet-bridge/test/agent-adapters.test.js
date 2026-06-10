import test from "node:test";
import assert from "node:assert/strict";

import { normalizeHermesHookEvent } from "../src/hermes-hook.js";
import { parseHermesLogActivity } from "../src/hermes-log-sync.js";
import { buildHermesLogSyncNotifications } from "../src/hermes-log-channel.js";
import {
  buildOpenClawAgentEvent,
  buildOpenClawToolEvent,
  buildOpenClawToolResultEvent
} from "../integrations/openclaw-pet-bridge/index.mjs";

test("Hermes pre_tool_call maps search-like tools to searching", () => {
  const event = normalizeHermesHookEvent({
    hook_event_name: "pre_tool_call",
    tool_name: "web_search",
    session_id: "sess-hermes-1",
    extra: {
      user_message: "Find a better pet sprite"
    }
  });

  assert.equal(event.source, "hermes");
  assert.equal(event.status, "searching");
  assert.equal(event.task, "Find a better pet sprite");
});

test("Hermes approval hook maps to needs-attention", () => {
  const event = normalizeHermesHookEvent({
    hook_event_name: "pre_approval_request",
    tool_name: "terminal",
    session_id: "sess-hermes-2",
    extra: {
      user_message: "Run a release command"
    }
  });

  assert.equal(event.status, "needs-attention");
  assert.equal(event.notify, true);
});

test("Hermes log turn start becomes an active Hermes channel", () => {
  const log = [
    "2026-06-10 09:44:33,904 INFO [20260610_094430_2bac8b] agent.turn_context: conversation turn: session=20260610_094430_2bac8b model=MiniMax-M3 provider=custom platform=tui history=0 msg='你好'"
  ].join("\n");
  const activity = parseHermesLogActivity(log, {
    nowMs: Date.parse("2026-06-10T09:44:40.000"),
    activeWindowMs: 60_000
  });

  assert.equal(activity.source, "hermes");
  assert.equal(activity.active, true);
  assert.equal(activity.sessionId, "20260610_094430_2bac8b");
  assert.equal(activity.task, "你好");

  const { notifications } = buildHermesLogSyncNotifications({}, activity, {
    nowMs: Date.parse("2026-06-10T09:44:40.000"),
    refreshMs: 90_000
  });

  assert.deepEqual(notifications, [
    [
      "--source", "hermes",
      "--task", "你好",
      "--status", "thinking",
      "--message", "Hermes is thinking",
      "--session-id", "20260610_094430_2bac8b",
      "--no-notify"
    ]
  ]);
});

test("Hermes log turn end becomes a completed Hermes channel", () => {
  const log = [
    "2026-06-10 09:44:33,904 INFO [20260610_094430_2bac8b] agent.turn_context: conversation turn: session=20260610_094430_2bac8b model=MiniMax-M3 provider=custom platform=tui history=0 msg='你好'",
    "2026-06-10 09:44:49,160 INFO [20260610_094430_2bac8b] agent.conversation_loop: Turn ended: reason=text_response(finish_reason=stop) model=MiniMax-M3 api_calls=1/90 budget=1/90 tool_turns=0 last_msg_role=assistant response_len=39 session=20260610_094430_2bac8b"
  ].join("\n");
  const activity = parseHermesLogActivity(log, {
    nowMs: Date.parse("2026-06-10T09:45:00.000"),
    completedWindowMs: 60_000
  });

  assert.equal(activity.source, "hermes");
  assert.equal(activity.completed, true);
  assert.equal(activity.sessionId, "20260610_094430_2bac8b");

  const state = { hermesActive: true };
  const { notifications } = buildHermesLogSyncNotifications(state, activity, {
    nowMs: Date.parse("2026-06-10T09:45:00.000")
  });

  assert.deepEqual(notifications, [
    [
      "--source", "hermes",
      "--task", "你好",
      "--status", "completed",
      "--message", "Hermes finished the turn",
      "--session-id", "20260610_094430_2bac8b",
      "--no-notify"
    ]
  ]);
});

test("Hermes completed channel refreshes when the bridge lost in-memory slots", () => {
  const activity = {
    source: "hermes",
    task: "你好",
    sessionId: "20260610_094430_2bac8b",
    active: false,
    completed: true,
    completedKey: "20260610_094430_2bac8b:1781055889160",
    endedAtMs: 1781055889160
  };
  const state = {
    hermesActive: false,
    hermesLastCompletedKey: "20260610_094430_2bac8b:1781055889160"
  };

  const { notifications } = buildHermesLogSyncNotifications(state, activity, {
    nowMs: Date.parse("2026-06-10T09:51:30.000"),
    refreshMs: 90_000
  });

  assert.equal(notifications.length, 1);
  assert.equal(notifications[0][1], "hermes");
  assert.equal(notifications[0][5], "completed");
});

test("OpenClaw before_agent_start builds a thinking event", () => {
  const event = buildOpenClawAgentEvent(
    {
      prompt: "Summarize the bridge state and continue"
    },
    {
      sessionKey: "openclaw-session-1",
      workspaceDir: "/tmp/openclaw",
      agentId: "main"
    },
    {
      source: "openclaw"
    }
  );

  assert.equal(event.source, "openclaw");
  assert.equal(event.status, "thinking");
  assert.equal(event.task, "Summarize the bridge state and continue");
});

test("OpenClaw before_tool_call maps search-like tools to searching", () => {
  const event = buildOpenClawToolEvent(
    {
      toolName: "search_query",
      params: {}
    },
    {
      sessionKey: "openclaw-session-2",
      workspaceDir: "/tmp/openclaw",
      agentId: "main"
    },
    {
      source: "openclaw"
    }
  );

  assert.equal(event.status, "searching");
  assert.equal(event.tool, "search_query");
});

test("OpenClaw after_tool_call falls back to thinking after normal tool completion", () => {
  const event = buildOpenClawToolResultEvent(
    {
      toolName: "apply_patch",
      params: {},
      result: { ok: true }
    },
    {
      sessionKey: "openclaw-session-3",
      workspaceDir: "/tmp/openclaw",
      agentId: "main"
    },
    {
      source: "openclaw"
    }
  );

  assert.equal(event.status, "thinking");
  assert.equal(event.notify, false);
});

test("OpenClaw after_tool_call surfaces approval-pending as needs-attention", () => {
  const event = buildOpenClawToolResultEvent(
    {
      toolName: "exec",
      params: { command: "git push" },
      result: { status: "approval-pending" }
    },
    {
      sessionKey: "openclaw-session-4",
      workspaceDir: "/tmp/openclaw",
      agentId: "main"
    },
    {
      source: "openclaw"
    }
  );

  assert.equal(event.status, "needs-attention");
  assert.equal(event.notify, true);
});
