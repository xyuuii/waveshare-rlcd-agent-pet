#!/usr/bin/env node
import { mkdir, readFile, rename, writeFile } from "node:fs/promises";
import { randomUUID } from "node:crypto";
import { homedir } from "node:os";
import { dirname, join, resolve } from "node:path";

const DEFAULT_BRIDGE_URL = process.env.PET_BRIDGE_URL || "http://127.0.0.1:17366/events";
const DEFAULT_QUEUE_PATH = process.env.PET_NOTIFY_QUEUE || join(homedir(), ".codex-pet-bridge", "notify-outbox.jsonl");
const SEND_TIMEOUT_MS = numberFromEnv("PET_NOTIFY_TIMEOUT_MS", 1200);
const MAX_FLUSH = numberFromEnv("PET_NOTIFY_MAX_FLUSH", 25);
const MAX_QUEUE = numberFromEnv("PET_NOTIFY_MAX_QUEUE", 300);

const args = parseArgs(process.argv.slice(2));
const bridgeUrl = args["bridge-url"] || DEFAULT_BRIDGE_URL;
const queuePath = resolve(args["queue"] || DEFAULT_QUEUE_PATH);

if (args.help) {
  printHelp();
  process.exit(0);
}

if (args.flush) {
  await flushQueue(bridgeUrl, queuePath);
  process.exit(0);
}

const required = ["source", "status", "message"];
for (const key of required) {
  if (!args[key]) {
    console.error(`Missing required option: --${key}`);
    printHelp();
    process.exit(2);
  }
}

const event = {
  id: args.id || randomUUID(),
  time: new Date().toISOString(),
  source: args.source,
  task: args.task || "",
  status: args.status,
  message: String(args.message).slice(0, numberFromEnv("PET_NOTIFY_MAX_MESSAGE_CHARS", 500)),
  workspace: args.workspace || "",
  sessionId: args["session-id"] || "",
  priority: args.priority || "normal"
};

if (args.notify) event.notify = true;
if (args["no-notify"]) event.notify = false;

if (args["dry-run"]) {
  console.log(JSON.stringify(event, null, 2));
  process.exit(0);
}

await flushQueue(bridgeUrl, queuePath);
const result = await sendEvent(bridgeUrl, event);
if (!result.ok) {
  await enqueue(queuePath, event, result.error);
}

function parseArgs(values) {
  const output = {};
  for (let index = 0; index < values.length; index += 1) {
    const value = values[index];
    if (!value.startsWith("--")) continue;
    const key = value.slice(2);
    if (["flush", "notify", "no-notify", "dry-run", "help"].includes(key)) {
      output[key] = true;
      continue;
    }
    output[key] = values[index + 1] || "";
    index += 1;
  }
  return output;
}

async function flushQueue(targetUrl, path) {
  let lines = [];
  try {
    lines = (await readFile(path, "utf8")).split("\n");
  } catch {
    return;
  }

  const remaining = [];
  let sent = 0;
  for (const line of lines.filter(Boolean).slice(-MAX_QUEUE)) {
    let record;
    try {
      record = JSON.parse(line);
    } catch {
      continue;
    }
    const event = { ...record };
    delete event._queuedAt;
    delete event._queueReason;
    if (sent < MAX_FLUSH) {
      const result = await sendEvent(targetUrl, event);
      if (result.ok) {
        sent += 1;
        continue;
      }
      record._queueReason = result.error;
    }
    remaining.push(record);
  }
  await writeQueue(path, remaining.slice(-MAX_QUEUE));
}

async function enqueue(path, event, reason) {
  let records = [];
  try {
    records = (await readFile(path, "utf8"))
      .split("\n")
      .filter(Boolean)
      .map((line) => JSON.parse(line));
  } catch {
    records = [];
  }
  records.push({
    ...event,
    _queuedAt: new Date().toISOString(),
    _queueReason: String(reason || "send-failed").slice(0, 160)
  });
  await writeQueue(path, records.slice(-MAX_QUEUE));
}

async function writeQueue(path, records) {
  await mkdir(dirname(path), { recursive: true });
  if (!records.length) {
    await writeFile(path, "", "utf8");
    return;
  }
  const tmpPath = `${path}.${process.pid}.${Date.now()}.${Math.random().toString(16).slice(2)}.tmp`;
  await writeFile(tmpPath, `${records.map((item) => JSON.stringify(item)).join("\n")}\n`, "utf8");
  await rename(tmpPath, path);
}

async function sendEvent(targetUrl, event) {
  const guard = allowedTarget(targetUrl);
  if (!guard.ok) return guard;
  try {
    const headers = { "content-type": "application/json" };
    const token = process.env.PET_BRIDGE_TOKEN || "";
    if (token) headers.authorization = `Bearer ${token}`;
    const response = await fetch(targetUrl, {
      method: "POST",
      headers,
      body: JSON.stringify(event),
      signal: AbortSignal.timeout(SEND_TIMEOUT_MS)
    });
    if (!response.ok) return { ok: false, error: `${response.status} ${response.statusText}` };
    return { ok: true };
  } catch (error) {
    return { ok: false, error: error.message || String(error) };
  }
}

function allowedTarget(targetUrl) {
  try {
    const url = new URL(targetUrl);
    const host = url.hostname.toLowerCase();
    const loopback = ["127.0.0.1", "::1", "localhost"].includes(host);
    const hasToken = Boolean(process.env.PET_BRIDGE_TOKEN || url.searchParams.get("token"));
    if (loopback || process.env.PET_NOTIFY_ALLOW_REMOTE === "1" || hasToken) return { ok: true };
    return { ok: false, error: "remote-target-requires-token-or-PET_NOTIFY_ALLOW_REMOTE" };
  } catch (error) {
    return { ok: false, error: error.message || String(error) };
  }
}

function numberFromEnv(name, fallback) {
  const value = Number(process.env[name]);
  return Number.isFinite(value) && value > 0 ? value : fallback;
}

function printHelp() {
  console.log(`Usage:
  pet-notify --source <name> --status <status> --message <text> [options]

Options:
  --task <id>              Stable task/session name.
  --workspace <path>       Optional workspace label.
  --session-id <id>        Optional upstream session id.
  --priority <level>       low, normal, high, or urgent.
  --notify                 Force unread notification creation.
  --no-notify              Suppress unread notification creation.
  --bridge-url <url>       Defaults to PET_BRIDGE_URL or localhost.
  --queue <path>           Defaults to ~/.codex-pet-bridge/notify-outbox.jsonl.
  --flush                  Retry queued events and exit.
  --dry-run                Print the event without sending.
`);
}
