import assert from "node:assert/strict";
import { appendFile, mkdir, mkdtemp, rm, utimes, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";

import {
  claudeTokensToday,
  contextFromUsage,
  inferClaudeStateFromEntries,
  loadClaudeRealtimeState,
  shortModelName,
  toolStatus
} from "../src/claude-session-status.js";

const T0 = Date.parse("2026-09-29T09:00:00.000Z");

function at(offsetSeconds) {
  return new Date(T0 + offsetSeconds * 1000).toISOString();
}

function assistant(id, content, { stop = null, usage, offset }) {
  return {
    type: "assistant",
    sessionId: "sess-1",
    cwd: "/Users/me/project",
    timestamp: at(offset),
    message: {
      id,
      role: "assistant",
      model: "claude-opus-5-5-20260601",
      content,
      stop_reason: stop,
      usage: usage || { input_tokens: 10, cache_creation_input_tokens: 1000, cache_read_input_tokens: 50000, output_tokens: 200 }
    }
  };
}

function user(content, offset) {
  return { type: "user", sessionId: "sess-1", cwd: "/Users/me/project", timestamp: at(offset), message: { role: "user", content } };
}

const conversation = [
  user("fix the flaky test", 0),
  assistant("msg_1", [{ type: "thinking", thinking: "..." }], { offset: 2 }),
  assistant("msg_1", [{ type: "tool_use", id: "t1", name: "Grep", input: {} }], { stop: "tool_use", offset: 3 }),
  user([{ type: "tool_result", tool_use_id: "t1", content: "..." }], 4),
  assistant("msg_2", [{ type: "tool_use", id: "t2", name: "Bash", input: {} }], { stop: "tool_use", offset: 6 }),
  user([{ type: "tool_result", tool_use_id: "t2", content: "ok" }], 20),
  assistant("msg_3", [{ type: "text", text: "Done." }], { stop: "end_turn", offset: 25 })
];

test("state follows the newest transcript entry", () => {
  const states = conversation.map((_, index) => inferClaudeStateFromEntries(conversation.slice(0, index + 1))?.status);
  assert.deepEqual(states, ["thinking", "thinking", "searching", "thinking", "tool-use", "thinking", "completed"]);
  const interrupted = [...conversation.slice(0, 5), user([{ type: "text", text: "[Request interrupted by user]" }], 7)];
  assert.equal(inferClaudeStateFromEntries(interrupted).status, "completed");
  assert.equal(inferClaudeStateFromEntries([{ type: "summary", summary: "x" }]), null);
});

test("helpers classify tools, context windows and model names", () => {
  assert.equal(toolStatus("WebSearch"), "searching");
  assert.equal(toolStatus("Edit"), "tool-use");
  assert.deepEqual(contextFromUsage({ input_tokens: 5, cache_read_input_tokens: 50_000, output_tokens: 100 }, "claude-sonnet-5"), {
    used: 50_105,
    window: 200_000
  });
  assert.equal(contextFromUsage({ input_tokens: 250_000 }, "x").window, 1_000_000);
  assert.equal(contextFromUsage(null), null);
  assert.equal(shortModelName("claude-opus-5-5-20260601"), "OPUS-5-5");
});

async function withTranscript(entries, fn) {
  const root = await mkdtemp(join(tmpdir(), "claude-projects-"));
  const dir = join(root, "-Users-me-project");
  await mkdir(dir, { recursive: true });
  const path = join(dir, "sess-1.jsonl");
  await writeFile(path, `${entries.map((entry) => JSON.stringify(entry)).join("\n")}\n`);
  try {
    await fn({ root, path });
  } finally {
    await rm(root, { recursive: true, force: true });
  }
}

test("realtime state honours activity windows", async () => {
  await withTranscript(conversation.slice(0, 5), async ({ root, path }) => {
    await utimes(path, new Date(T0 + 6000), new Date(T0 + 6000));
    const running = await loadClaudeRealtimeState({ projectsDir: root, now: T0 + 60_000, sourcePrefix: "mac" });
    assert.equal(running.status, "tool-use");
    assert.equal(running.source, "mac-claude");
    assert.equal(running.task, "mac-claude-session");
    assert.equal(running.sessionId, "sess-1");
    assert.equal(running.workspace, "/Users/me/project");
    assert.equal(running.context.used, 51_210);
    // A tool may legitimately run for minutes, but not forever.
    assert.equal(await loadClaudeRealtimeState({ projectsDir: root, now: T0 + 20 * 60_000 }), null);
  });
  await withTranscript(conversation, async ({ root, path }) => {
    await utimes(path, new Date(T0 + 25000), new Date(T0 + 25000));
    assert.equal((await loadClaudeRealtimeState({ projectsDir: root, now: T0 + 30_000 })).status, "completed");
    assert.equal(await loadClaudeRealtimeState({ projectsDir: root, now: T0 + 90_000 }), null);
  });
  assert.equal(await loadClaudeRealtimeState({ projectsDir: "/nonexistent/claude" }), null);
});

test("today's tokens count each API message once and grow incrementally", async () => {
  await withTranscript(conversation, async ({ root, path }) => {
    const now = T0 + 60_000;
    await utimes(path, new Date(now), new Date(now));
    // msg_1 appears twice (two content blocks) but counts once: 3 messages x 1210.
    assert.equal(await claudeTokensToday({ projectsDir: root, now }), 3 * 1210);
    await appendFile(path, `${JSON.stringify(assistant("msg_4", [{ type: "text", text: "more" }], { stop: "end_turn", offset: 40 }))}\n`);
    await utimes(path, new Date(now), new Date(now));
    assert.equal(await claudeTokensToday({ projectsDir: root, now }), 4 * 1210);
  });
});
