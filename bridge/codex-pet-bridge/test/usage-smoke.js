import { mkdtemp, mkdir, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { execFile } from "node:child_process";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const tempRoot = await mkdtemp(join(tmpdir(), "codex-pet-usage-"));
const sessionsRoot = join(tempRoot, "sessions");
const dbPath = join(tempRoot, "state.sqlite");
const port = 17369;
const cwdA = "/workspace/current";
const cwdB = "/workspace/other";
const today = new Date();
const yyyy = String(today.getFullYear());
const mm = String(today.getMonth() + 1).padStart(2, "0");
const dd = String(today.getDate()).padStart(2, "0");

const rolloutA = join(sessionsRoot, yyyy, mm, dd, "rollout-a.jsonl");
const rolloutB = join(sessionsRoot, yyyy, mm, dd, "rollout-b.jsonl");

await mkdir(dirname(rolloutA), { recursive: true });
await writeRollout(
  rolloutA,
  123_456,
  34_567,
  258_400,
  12,
  8
);
await writeRollout(
  rolloutB,
  999_999,
  11_111,
  258_400,
  40,
  30
);
await createStateDb(dbPath, [
  {
    id: "thread-other",
    rolloutPath: rolloutB,
    source: "codex",
    cwd: cwdB,
    title: "other"
  },
  {
    id: "thread-current",
    rolloutPath: rolloutA,
    source: "codex",
    cwd: cwdA,
    title: "current"
  }
]);

const server = spawn(process.execPath, ["./src/bridge-server.js"], {
  cwd: process.cwd(),
  env: {
    ...process.env,
    PET_BRIDGE_PORT: String(port),
    PET_BRIDGE_LOG: join(tempRoot, "events.jsonl"),
    PET_BRIDGE_STATE: join(tempRoot, "bridge-state.json"),
    PET_BRIDGE_CODEX_STATE: dbPath,
    PET_BRIDGE_CODEX_CWD: cwdA,
    PET_BRIDGE_CODEX_SESSIONS: sessionsRoot,
    PET_BRIDGE_USAGE_CACHE_MS: "0"
  },
  stdio: ["ignore", "pipe", "pipe"]
});

try {
  await waitForServer(port);
  const response = await fetch(`http://127.0.0.1:${port}/esp32/poll`);
  const body = await response.json();

  if (!body.ok) throw new Error("poll endpoint not ok");
  if (body.usage?.today !== "123k tok") {
    throw new Error(`expected today usage for current cwd only, got ${body.usage?.today}`);
  }
  if (body.usage?.context !== "34.6k / 258k") {
    throw new Error(`unexpected context label: ${body.usage?.context}`);
  }
  if (body.usage?.quota !== "5H 88% WK 92%") {
    throw new Error(`unexpected quota label: ${body.usage?.quota}`);
  }
  if (body.usage?.today_label !== "TODAY" || body.usage?.quota_style !== "quota") {
    throw new Error(`expected Codex usage metadata, got ${JSON.stringify(body.usage)}`);
  }

  const now = Date.now();
  await fetch(`http://127.0.0.1:${port}/events`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({
      source: "codex",
      sessionId: "sess-codex",
      task: "workspace review",
      type: "status",
      status: "thinking",
      message: "Codex is thinking",
      time: new Date(now - 20_000).toISOString(),
      notify: false
    })
  });
  await fetch(`http://127.0.0.1:${port}/events`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({
      source: "hermes",
      task: "pet review",
      type: "pre_tool_call",
      status: "searching",
      message: "Hermes is searching",
      time: new Date(now - 65_000).toISOString(),
      notify: false
    })
  });
  await fetch(`http://127.0.0.1:${port}/events`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({
      source: "hermes",
      task: "pet review",
      type: "pre_llm_call",
      status: "thinking",
      message: "Hermes is thinking",
      time: new Date(now - 5_000).toISOString(),
      notify: false
    })
  });

  const hermesResponse = await fetch(`http://127.0.0.1:${port}/esp32/poll`);
  const hermesBody = await hermesResponse.json();

  if (hermesBody.source !== "hermes") {
    throw new Error(`expected Hermes to own current usage panel, got ${hermesBody.source}`);
  }
  if (hermesBody.usage?.today_label !== "SESS" || hermesBody.usage?.context_label !== "TOOLS") {
    throw new Error(`expected generic usage labels, got ${JSON.stringify(hermesBody.usage)}`);
  }
  if (hermesBody.usage?.quota_label !== "ATTN" || hermesBody.usage?.quota_style !== "text") {
    throw new Error(`expected generic attention card, got ${JSON.stringify(hermesBody.usage)}`);
  }
  if (hermesBody.usage?.context !== "1 call") {
    throw new Error(`expected one tool call, got ${hermesBody.usage?.context}`);
  }
  if (hermesBody.usage?.quota !== "CLEAR") {
    throw new Error(`expected clear attention state, got ${hermesBody.usage?.quota}`);
  }
  if (!hermesBody.usage?.today || hermesBody.usage.today === "--") {
    throw new Error(`expected session runtime, got ${hermesBody.usage?.today}`);
  }

  const focusAuto = await fetch(`http://127.0.0.1:${port}/esp32/poll?focus=auto`);
  const focusAutoBody = await focusAuto.json();
  if (!Array.isArray(focusAutoBody.agents) || focusAutoBody.agents.length < 2) {
    throw new Error(`expected compact slot list, got ${JSON.stringify(focusAutoBody)}`);
  }
  if (focusAutoBody.focus_mode !== "auto" || focusAutoBody.focus_index !== -1) {
    throw new Error(`expected auto focus metadata, got ${JSON.stringify(focusAutoBody)}`);
  }
  if (focusAutoBody.focus_id !== "" || focusAutoBody.focus_count !== focusAutoBody.agents.length) {
    throw new Error(`expected auto focus counters, got ${JSON.stringify(focusAutoBody)}`);
  }

  const pinnedHermesSlot = focusAutoBody.agents.find((item) => item.source === "hermes");
  if (!pinnedHermesSlot) {
    throw new Error(`expected Hermes slot in compact list, got ${JSON.stringify(focusAutoBody.agents)}`);
  }
  const pinnedHermesResponse = await fetch(
    `http://127.0.0.1:${port}/esp32/poll?focus=${encodeURIComponent(pinnedHermesSlot.id)}`
  );
  const pinnedHermesBody = await pinnedHermesResponse.json();
  if (pinnedHermesBody.source !== "hermes" || pinnedHermesBody.focus_mode !== "pinned") {
    throw new Error(`expected hermes pinned focus, got ${JSON.stringify(pinnedHermesBody)}`);
  }
  if (pinnedHermesBody.focus_id !== pinnedHermesSlot.id || pinnedHermesBody.focus_index < 0) {
    throw new Error(`expected hermes focus identifiers, got ${JSON.stringify(pinnedHermesBody)}`);
  }
  if (
    pinnedHermesBody.usage?.today_label !== "SESS" ||
    pinnedHermesBody.usage?.context_label !== "TOOLS" ||
    pinnedHermesBody.usage?.quota_label !== "ATTN" ||
    pinnedHermesBody.usage?.quota_style !== "text"
  ) {
    throw new Error(`expected pinned Hermes generic usage, got ${JSON.stringify(pinnedHermesBody.usage)}`);
  }

  const pinnedCodexSlot = focusAutoBody.agents.find((item) => item.source === "codex");
  if (!pinnedCodexSlot) {
    throw new Error(`expected Codex slot in compact list, got ${JSON.stringify(focusAutoBody.agents)}`);
  }
  const pinnedCodexResponse = await fetch(
    `http://127.0.0.1:${port}/esp32/poll?focus=${encodeURIComponent(pinnedCodexSlot.id)}`
  );
  const pinnedCodexBody = await pinnedCodexResponse.json();
  if (pinnedCodexBody.source !== "codex" || pinnedCodexBody.focus_mode !== "pinned") {
    throw new Error(`expected codex pinned focus, got ${JSON.stringify(pinnedCodexBody)}`);
  }
  if (pinnedCodexBody.usage?.today_label !== "TODAY" || pinnedCodexBody.usage?.quota_style !== "quota") {
    throw new Error(`expected pinned Codex usage, got ${JSON.stringify(pinnedCodexBody.usage)}`);
  }

  const invalidFocus = await fetch(`http://127.0.0.1:${port}/esp32/poll?focus=slot_missing`);
  const invalidFocusBody = await invalidFocus.json();
  if (
    invalidFocusBody.focus_mode !== "auto" ||
    invalidFocusBody.focus_index !== -1 ||
    invalidFocusBody.source !== focusAutoBody.source
  ) {
    throw new Error(`expected invalid focus to fall back to auto, got ${JSON.stringify(invalidFocusBody)}`);
  }

  await fetch(`http://127.0.0.1:${port}/events`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({
      source: "hermes",
      sessionId: "sess-a",
      workspace: "/workspace/hermes-a",
      task: "shared focus",
      type: "pre_tool_call",
      status: "searching",
      message: "Hermes A searched",
      time: new Date(now - 20_000).toISOString(),
      notify: false
    })
  });
  await fetch(`http://127.0.0.1:${port}/events`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({
      source: "hermes",
      sessionId: "sess-a",
      workspace: "/workspace/hermes-a",
      task: "shared focus",
      type: "pre_llm_call",
      status: "thinking",
      message: "Hermes A is thinking",
      time: new Date(now - 10_000).toISOString(),
      notify: false
    })
  });
  await fetch(`http://127.0.0.1:${port}/events`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({
      source: "hermes",
      sessionId: "sess-b",
      workspace: "/workspace/hermes-b",
      task: "shared focus",
      type: "pre_tool_call",
      status: "searching",
      message: "Hermes B searched",
      time: new Date(now - 7_200_000).toISOString(),
      notify: false
    })
  });
  await fetch(`http://127.0.0.1:${port}/events`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({
      source: "hermes",
      sessionId: "sess-b",
      workspace: "/workspace/hermes-b",
      task: "shared focus",
      type: "status",
      status: "needs-attention",
      message: "Hermes B needs review",
      time: new Date(now - 4_000).toISOString(),
      notify: true
    })
  });

  const siblingFocusAuto = await fetch(`http://127.0.0.1:${port}/esp32/poll?focus=auto`);
  const siblingFocusAutoBody = await siblingFocusAuto.json();
  const pinnedSiblingSlot = siblingFocusAutoBody.agents.find(
    (item) => item.source === "hermes" && item.task === "shared focus" && item.status === "thinking"
  );
  if (!pinnedSiblingSlot) {
    throw new Error(`expected sibling Hermes slot for pinning, got ${JSON.stringify(siblingFocusAutoBody.agents)}`);
  }
  const pinnedSiblingResponse = await fetch(
    `http://127.0.0.1:${port}/esp32/poll?focus=${encodeURIComponent(pinnedSiblingSlot.id)}`
  );
  const pinnedSiblingBody = await pinnedSiblingResponse.json();
  if (pinnedSiblingBody.source !== "hermes" || pinnedSiblingBody.task !== "shared focus") {
    throw new Error(`expected pinned sibling session, got ${JSON.stringify(pinnedSiblingBody)}`);
  }
  if (pinnedSiblingBody.usage?.context !== "1 call") {
    throw new Error(`expected sibling pin to isolate tool count, got ${JSON.stringify(pinnedSiblingBody.usage)}`);
  }
  if (pinnedSiblingBody.usage?.quota !== "CLEAR") {
    throw new Error(`expected sibling pin to ignore other-session waiting state, got ${JSON.stringify(pinnedSiblingBody.usage)}`);
  }
  if (pinnedSiblingBody.usage?.today?.includes("h")) {
    throw new Error(`expected sibling pin to isolate elapsed time, got ${JSON.stringify(pinnedSiblingBody.usage)}`);
  }

  console.log("usage smoke ok");
} finally {
  server.kill();
  await rm(tempRoot, { recursive: true, force: true });
}

async function waitForServer(port) {
  for (let index = 0; index < 30; index += 1) {
    try {
      const response = await fetch(`http://127.0.0.1:${port}/health`);
      if (response.ok) return;
    } catch {
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
  }
  throw new Error("server did not start");
}

async function writeRollout(path, totalTokens, lastTurnTokens, contextWindow, primaryUsed, secondaryUsed) {
  const payload = {
    type: "event_msg",
    payload: {
      type: "token_count",
      info: {
        total_token_usage: { total_tokens: totalTokens },
        last_token_usage: { total_tokens: lastTurnTokens },
        model_context_window: contextWindow
      },
      rate_limits: {
        primary: { used_percent: primaryUsed },
        secondary: { used_percent: secondaryUsed }
      }
    }
  };
  await writeFile(path, `${JSON.stringify(payload)}\n`, "utf8");
}

async function createStateDb(path, rows) {
  await execFileAsync("sqlite3", [
    path,
    `
      create table threads (
        id text primary key,
        rollout_path text not null,
        created_at integer not null,
        updated_at integer not null,
        source text not null,
        model_provider text not null,
        cwd text not null,
        title text not null,
        sandbox_policy text not null,
        approval_mode text not null,
        tokens_used integer not null default 0,
        has_user_event integer not null default 0,
        archived integer not null default 0
      );
    `
  ]);

  for (const [index, row] of rows.entries()) {
    await execFileAsync("sqlite3", [
      path,
      `
        insert into threads (
          id, rollout_path, created_at, updated_at, source, model_provider,
          cwd, title, sandbox_policy, approval_mode, tokens_used, has_user_event, archived
        ) values (
          '${escapeSql(row.id)}',
          '${escapeSql(row.rolloutPath)}',
          ${1 + index},
          ${1 + index},
          '${escapeSql(row.source)}',
          'openai',
          '${escapeSql(row.cwd)}',
          '${escapeSql(row.title)}',
          'workspace-write',
          'never',
          0,
          1,
          0
        );
      `
    ]);
  }
}

function escapeSql(value) {
  return String(value).replaceAll("'", "''");
}
