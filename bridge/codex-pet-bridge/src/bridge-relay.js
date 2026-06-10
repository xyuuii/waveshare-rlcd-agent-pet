import { mkdir, readFile, rename, writeFile } from "node:fs/promises";
import { randomUUID } from "node:crypto";
import { homedir } from "node:os";
import { dirname, join, resolve } from "node:path";

const DEFAULT_BRIDGE_URL = process.env.PET_BRIDGE_URL || "http://127.0.0.1:17366/events";
const DEFAULT_QUEUE_PATH = resolve(process.env.PET_NOTIFY_QUEUE || join(homedir(), ".codex-pet-bridge", "notify-outbox.jsonl"));
const DEFAULT_TIMEOUT_MS = numberFromEnv("PET_BRIDGE_HOOK_TIMEOUT_MS", 1200);
const MAX_QUEUE = numberFromEnv("PET_NOTIFY_MAX_QUEUE", 300);
const MAX_MESSAGE_CHARS = numberFromEnv("PET_NOTIFY_MAX_MESSAGE_CHARS", 500);

export async function readStdinJson() {
  const chunks = [];
  for await (const chunk of process.stdin) chunks.push(chunk);
  const text = Buffer.concat(chunks).toString("utf8").trim();
  return text ? JSON.parse(text) : {};
}

export async function relayBridgeEvent(event, options = {}) {
  const normalizedEvent = {
    id: stringValue(event.id) || randomUUID(),
    time: stringValue(event.time) || new Date().toISOString(),
    source: stringValue(event.source) || "unknown",
    task: stringValue(event.task) || "",
    status: stringValue(event.status) || "event",
    message: truncate(stringValue(event.message) || "Agent event", MAX_MESSAGE_CHARS),
    workspace: stringValue(event.workspace) || "",
    sessionId: stringValue(event.sessionId) || "",
    tool: stringValue(event.tool) || ""
  };

  for (const [key, value] of Object.entries(event)) {
    if (!(key in normalizedEvent) && value !== undefined) {
      normalizedEvent[key] = value;
    }
  }

  const result = await postEvent(normalizedEvent, options);
  if (!result.ok) {
    await enqueueEvent(normalizedEvent, result.error, options.queuePath);
  }
  return { ...result, event: normalizedEvent };
}

export async function postEvent(event, options = {}) {
  const targetUrl = stringValue(options.bridgeUrl) || DEFAULT_BRIDGE_URL;
  const timeoutMs = positiveNumber(options.timeoutMs, DEFAULT_TIMEOUT_MS);
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const headers = { "content-type": "application/json" };
    const token = process.env.PET_BRIDGE_TOKEN || "";
    if (token) headers.authorization = `Bearer ${token}`;
    const response = await fetch(targetUrl, {
      method: "POST",
      headers,
      body: JSON.stringify(event),
      signal: controller.signal
    });
    if (!response.ok) return { ok: false, error: `${response.status} ${response.statusText}` };
    return { ok: true };
  } catch (error) {
    return { ok: false, error: error.message || String(error) };
  } finally {
    clearTimeout(timer);
  }
}

export async function enqueueEvent(event, reason, queuePath = DEFAULT_QUEUE_PATH) {
  const resolvedPath = resolve(stringValue(queuePath) || DEFAULT_QUEUE_PATH);
  let records = [];
  try {
    records = (await readFile(resolvedPath, "utf8"))
      .split("\n")
      .filter(Boolean)
      .map((line) => JSON.parse(line));
  } catch {
    records = [];
  }
  records.push({
    ...event,
    _queuedAt: new Date().toISOString(),
    _queueReason: truncate(String(reason || "send-failed"), 160)
  });
  await writeQueue(resolvedPath, records.slice(-MAX_QUEUE));
}

export function toolStatusForName(value) {
  const tool = stringValue(value).toLowerCase();
  if (!tool) return "tool-use";
  if (
    tool.includes("search") ||
    tool.includes("grep") ||
    tool.includes("find") ||
    tool.includes("glob") ||
    tool === "rg"
  ) {
    return "searching";
  }
  return "tool-use";
}

export function summarizeTask(...values) {
  for (const value of values) {
    const summary = summarizeValue(value);
    if (summary) return summary;
  }
  return "";
}

export function stringValue(value) {
  return typeof value === "string" ? value.trim() : "";
}

export function truncate(value, limit) {
  const text = String(value || "").replace(/\s+/g, " ").trim();
  if (!text) return "";
  return text.length > limit ? `${text.slice(0, Math.max(0, limit - 1))}…` : text;
}

function summarizeValue(value) {
  if (typeof value === "string") {
    return truncate(firstLine(value), 72);
  }
  if (Array.isArray(value)) {
    for (const item of value) {
      const summary = summarizeValue(item);
      if (summary) return summary;
    }
    return "";
  }
  if (value && typeof value === "object") {
    if (typeof value.text === "string") return truncate(firstLine(value.text), 72);
    if (typeof value.content === "string") return truncate(firstLine(value.content), 72);
  }
  return "";
}

function firstLine(value) {
  return String(value || "").split(/\r?\n/).map((item) => item.trim()).find(Boolean) || "";
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

function numberFromEnv(name, fallback) {
  return positiveNumber(process.env[name], fallback);
}

function positiveNumber(value, fallback) {
  const parsed = Number(value);
  return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback;
}
