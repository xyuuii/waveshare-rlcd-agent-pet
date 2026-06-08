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
