#!/usr/bin/env node
import { mkdir, readFile, rename, writeFile } from "node:fs/promises";
import { homedir } from "node:os";
import { dirname, join, resolve } from "node:path";

const BRIDGE_URL = process.env.PET_BRIDGE_URL || "http://127.0.0.1:17366/events";
const TIMEOUT_MS = Number(process.env.PET_BRIDGE_HOOK_TIMEOUT_MS || 1200);
const QUEUE_PATH = resolve(process.env.PET_NOTIFY_QUEUE || join(homedir(), ".codex-pet-bridge", "notify-outbox.jsonl"));
const MAX_QUEUE = Number(process.env.PET_NOTIFY_MAX_QUEUE || 300);

try {
  const input = await readStdinJson();
  const event = {
    ...input,
    source: input.source || "claude-code",
    type: input.hook_event_name || input.type || "hook",
    status: input.status || statusForHook(input.hook_event_name, input.tool_name || input.tool),
    message: input.message || messageForHook(input)
  };

  const result = await postEvent(event);
  if (!result.ok) await enqueueEvent(event, result.error);
  process.exit(0);
} catch (error) {
  // Hooks should be observational. Never block Claude Code because the pet is offline.
  console.error(`pet-claude-hook ignored error: ${error.message || String(error)}`);
  process.exit(0);
}

async function readStdinJson() {
  const chunks = [];
  for await (const chunk of process.stdin) chunks.push(chunk);
  const text = Buffer.concat(chunks).toString("utf8").trim();
  return text ? JSON.parse(text) : {};
}

async function postEvent(event) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), TIMEOUT_MS);
  try {
    const response = await fetch(BRIDGE_URL, {
      method: "POST",
      headers: { "content-type": "application/json" },
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

async function enqueueEvent(event, reason) {
  let records = [];
  try {
    records = (await readFile(QUEUE_PATH, "utf8"))
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
  await mkdir(dirname(QUEUE_PATH), { recursive: true });
  const tmpPath = `${QUEUE_PATH}.${process.pid}.${Date.now()}.${Math.random().toString(16).slice(2)}.tmp`;
  await writeFile(tmpPath, `${records.slice(-MAX_QUEUE).map((item) => JSON.stringify(item)).join("\n")}\n`, "utf8");
  await rename(tmpPath, QUEUE_PATH);
}

function statusForHook(name, toolName) {
  if (name === "Notification") return "needs-attention";
  if (name === "Stop") return "completed";
  if (name === "SessionStart") return "started";
  if (name === "UserPromptSubmit") return "thinking";
  if (name === "PreToolUse" || name === "PostToolUse") return toolStatusForName(toolName);
  return "event";
}

function toolStatusForName(value) {
  const tool = String(value || "").toLowerCase();
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

function messageForHook(input) {
  if (input.notification?.message) return input.notification.message;
  if (input.tool_name) return `${input.hook_event_name}: ${input.tool_name}`;
  if (input.prompt) return "User prompt submitted";
  if (input.hook_event_name === "Stop") return "Claude Code task completed";
  return input.hook_event_name || "Claude Code event";
}
