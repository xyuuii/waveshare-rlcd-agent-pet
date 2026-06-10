import test from "node:test";
import assert from "node:assert/strict";

import {
  buildVisibleAgentSlots,
  resolveFocusSelection
} from "../src/device-focus.js";

function event(overrides = {}) {
  return {
    id: overrides.id || "evt-1",
    source: overrides.source || "codex",
    task: overrides.task || "task-a",
    status: overrides.status || "thinking",
    time: overrides.time || "2026-06-09T10:00:00.000Z",
    sessionId: overrides.sessionId || "sess-a",
    workspace: overrides.workspace || "/repo/a",
    tool: overrides.tool || ""
  };
}

test("buildVisibleAgentSlots keeps two codex sessions separate", () => {
  const slots = buildVisibleAgentSlots([
    event({ source: "codex", sessionId: "sess-a", task: "task-a" }),
    event({
      source: "codex",
      sessionId: "sess-b",
      task: "task-b",
      time: "2026-06-09T10:00:05.000Z"
    })
  ]);

  assert.equal(slots.length, 2);
  assert.notEqual(slots[0].id, slots[1].id);
});

test("resolveFocusSelection falls back to auto for stale slot ids", () => {
  const slots = [buildVisibleAgentSlots([event()])[0]];
  const resolved = resolveFocusSelection({
    focus: "slot_missing",
    slots,
    autoCurrent: slots[0]
  });

  assert.equal(resolved.mode, "auto");
  assert.equal(resolved.current.id, slots[0].id);
});

test("buildVisibleAgentSlots keeps delimiter-containing identity fields separate", () => {
  const slots = buildVisibleAgentSlots([
    event({
      source: "codex",
      sessionId: "sess|1",
      workspace: "/repo",
      task: "review"
    }),
    event({
      source: "codex|sess",
      sessionId: "1",
      workspace: "/repo",
      task: "review",
      time: "2026-06-09T10:00:05.000Z"
    })
  ]);

  assert.equal(slots.length, 2);
});

test("buildVisibleAgentSlots assigns distinct ids for long shared key prefixes", () => {
  const slots = buildVisibleAgentSlots([
    event({
      source: "aaaaaaaaaaaaX",
      sessionId: "sess-a",
      workspace: "/repo",
      task: "review"
    }),
    event({
      source: "aaaaaaaaaaaaY",
      sessionId: "sess-a",
      workspace: "/repo",
      task: "review",
      time: "2026-06-09T10:00:05.000Z"
    })
  ]);

  assert.equal(slots.length, 2);
  assert.notEqual(slots[0].id, slots[1].id);
});

test("buildVisibleAgentSlots keeps one slot when later events enrich the same session", () => {
  const slots = buildVisibleAgentSlots([
    event({
      source: "hermes",
      sessionId: "sess-a",
      task: "shared focus",
      workspace: "",
      time: "2026-06-09T10:00:00.000Z"
    }),
    event({
      source: "hermes",
      sessionId: "sess-a",
      task: "shared focus",
      workspace: "/repo/a",
      status: "searching",
      time: "2026-06-09T10:00:05.000Z"
    })
  ]);

  assert.equal(slots.length, 1);
  assert.equal(slots[0].sessionId, "sess-a");
  assert.equal(slots[0].workspace, "/repo/a");
  assert.equal(slots[0].status, "searching");
});

test("buildVisibleAgentSlots merges a unique workspace-only slot with later session identity", () => {
  const workspaceOnly = event({
    source: "hermes",
    sessionId: "",
    workspace: "/repo/a",
    task: "shared focus",
    time: "2026-06-09T10:00:00.000Z"
  });
  const enriched = event({
    source: "hermes",
    sessionId: "sess-a",
    workspace: "",
    task: "shared focus",
    status: "searching",
    time: "2026-06-09T10:00:05.000Z"
  });
  const workspaceSlots = buildVisibleAgentSlots([workspaceOnly]);
  const slots = buildVisibleAgentSlots([workspaceOnly, enriched]);

  assert.equal(slots.length, 1);
  assert.equal(slots[0].sessionId, "sess-a");
  assert.equal(slots[0].workspace, "/repo/a");
  assert.equal(slots[0].status, "searching");

  const resolved = resolveFocusSelection({
    focus: workspaceSlots[0].id,
    slots,
    autoCurrent: slots[0]
  });
  assert.equal(resolved.mode, "pinned");
  assert.equal(resolved.current.id, slots[0].id);
});

test("buildVisibleAgentSlots keeps a stable slot id as retained history rolls forward", () => {
  const initial = buildVisibleAgentSlots([
    event({
      source: "hermes",
      sessionId: "sess-a",
      workspace: "",
      task: "shared focus",
      time: "2026-06-09T10:00:00.000Z"
    }),
    event({
      source: "hermes",
      sessionId: "sess-a",
      workspace: "/repo/a",
      task: "shared focus",
      status: "searching",
      time: "2026-06-09T10:00:05.000Z"
    })
  ]);
  const rolled = buildVisibleAgentSlots([
    event({
      source: "hermes",
      sessionId: "sess-a",
      workspace: "/repo/a",
      task: "shared focus",
      status: "searching",
      time: "2026-06-09T10:00:05.000Z"
    }),
    event({
      source: "hermes",
      sessionId: "sess-a",
      workspace: "/repo/a",
      task: "shared focus",
      status: "thinking",
      time: "2026-06-09T10:00:10.000Z"
    })
  ]);

  assert.equal(initial.length, 1);
  assert.equal(rolled.length, 1);
  assert.equal(initial[0].id, rolled[0].id);
});

test("weak task-only pins stay with the original sibling instead of jumping to another session", () => {
  const weak = event({
    source: "hermes",
    sessionId: "",
    workspace: "",
    task: "shared focus",
    time: "2026-06-09T10:00:00.000Z"
  });
  const weakSlots = buildVisibleAgentSlots([weak]);
  const slots = buildVisibleAgentSlots([
    weak,
    event({
      source: "hermes",
      sessionId: "sess-a",
      workspace: "/repo/a",
      task: "shared focus",
      status: "thinking",
      time: "2026-06-09T10:00:05.000Z"
    }),
    event({
      source: "hermes",
      sessionId: "sess-b",
      workspace: "/repo/b",
      task: "shared focus",
      status: "needs-attention",
      time: "2026-06-09T10:00:10.000Z"
    })
  ]);

  const resolved = resolveFocusSelection({
    focus: weakSlots[0].id,
    slots,
    autoCurrent: slots[0]
  });

  assert.equal(resolved.mode, "pinned");
  assert.equal(resolved.current.sessionId, "sess-a");
  assert.equal(resolved.current.workspace, "/repo/a");
});

test("workspace-first pins stay with the original sibling instead of jumping to a later session", () => {
  const workspaceOnly = event({
    source: "hermes",
    sessionId: "",
    workspace: "/repo/a",
    task: "shared focus",
    time: "2026-06-09T10:00:00.000Z"
  });
  const workspaceSlots = buildVisibleAgentSlots([workspaceOnly]);
  const slots = buildVisibleAgentSlots([
    workspaceOnly,
    event({
      source: "hermes",
      sessionId: "sess-a",
      workspace: "/repo/a",
      task: "shared focus",
      status: "thinking",
      time: "2026-06-09T10:00:05.000Z"
    }),
    event({
      source: "hermes",
      sessionId: "sess-b",
      workspace: "/repo/a",
      task: "shared focus",
      status: "needs-attention",
      time: "2026-06-09T10:00:10.000Z"
    })
  ]);

  const resolved = resolveFocusSelection({
    focus: workspaceSlots[0].id,
    slots,
    autoCurrent: slots[0]
  });

  assert.equal(resolved.mode, "pinned");
  assert.equal(resolved.current.sessionId, "sess-a");
  assert.equal(resolved.current.workspace, "/repo/a");
});

test("later weak events do not retarget an already-migrated weak pin to another sibling", () => {
  const weak = event({
    source: "hermes",
    sessionId: "",
    workspace: "",
    task: "shared focus",
    time: "2026-06-09T10:00:00.000Z"
  });
  const weakSlots = buildVisibleAgentSlots([weak]);
  const slots = buildVisibleAgentSlots([
    weak,
    event({
      source: "hermes",
      sessionId: "sess-a",
      workspace: "/repo/a",
      task: "shared focus",
      status: "thinking",
      time: "2026-06-09T10:00:05.000Z"
    }),
    event({
      source: "hermes",
      sessionId: "sess-b",
      workspace: "/repo/b",
      task: "shared focus",
      status: "needs-attention",
      time: "2026-06-09T10:00:10.000Z"
    }),
    event({
      source: "hermes",
      sessionId: "",
      workspace: "",
      task: "shared focus",
      status: "thinking",
      time: "2026-06-09T10:00:15.000Z"
    })
  ]);

  const resolved = resolveFocusSelection({
    focus: weakSlots[0].id,
    slots,
    autoCurrent: slots[0]
  });

  assert.equal(resolved.mode, "pinned");
  assert.equal(resolved.current.sessionId, "sess-a");
  assert.equal(resolved.current.workspace, "/repo/a");
});

test("needs-attention sorts ahead of active work", () => {
  const slots = buildVisibleAgentSlots([
    event({
      source: "hermes",
      sessionId: "sess-a",
      status: "thinking",
      time: "2026-06-09T10:00:02.000Z"
    }),
    event({
      source: "openclaw",
      sessionId: "sess-b",
      status: "needs-attention",
      time: "2026-06-09T10:00:01.000Z"
    })
  ]);

  assert.equal(slots[0].status, "needs-attention");
});
