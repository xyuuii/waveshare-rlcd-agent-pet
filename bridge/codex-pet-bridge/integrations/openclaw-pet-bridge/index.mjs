import { randomUUID } from "node:crypto";
import { readFileSync } from "node:fs";
import { mkdir, readFile, rename, writeFile } from "node:fs/promises";
import { homedir } from "node:os";
import { dirname, join, resolve } from "node:path";

const DEFAULT_BRIDGE_URL = process.env.PET_BRIDGE_URL || "http://127.0.0.1:17366/events";
const DEFAULT_QUEUE_PATH = resolve(process.env.PET_NOTIFY_QUEUE || join(homedir(), ".codex-pet-bridge", "notify-outbox.jsonl"));
const DEFAULT_TIMEOUT_MS = positiveNumber(process.env.PET_BRIDGE_HOOK_TIMEOUT_MS, 1200);
const MAX_QUEUE = positiveNumber(process.env.PET_NOTIFY_MAX_QUEUE, 300);

const plugin = {
  id: "openclaw-pet-bridge",
  name: "OpenClaw Pet Bridge",
  description: "Forward OpenClaw runtime status to codex-pet-bridge.",
  configSchema: {
    type: "object",
    additionalProperties: false,
    properties: {
      bridgeUrl: { type: "string" },
      source: { type: "string" },
      taskPrefix: { type: "string" },
      notifyOnCompleted: { type: "boolean" },
      notifyOnError: { type: "boolean" }
    }
  },
  register(api) {
    const config = normalizePluginConfig(api.pluginConfig);
    const taskBySession = new Map();

    api.on("before_agent_start", async (event, ctx) => {
      const task = resolveTask(event.prompt, ctx, config);
      taskBySession.set(sessionKey(ctx), task);
      await emitEvent(buildOpenClawAgentEvent(event, ctx, config, task));
    });

    api.on("before_tool_call", async (event, ctx) => {
      await emitEvent(buildOpenClawToolEvent(event, ctx, config, taskBySession.get(sessionKey(ctx))));
    });

    api.on("after_tool_call", async (event, ctx) => {
      await emitEvent(buildOpenClawToolResultEvent(event, ctx, config, taskBySession.get(sessionKey(ctx))));
    });

    api.on("agent_end", async (event, ctx) => {
      await emitEvent(buildOpenClawAgentEndEvent(event, ctx, config, taskBySession.get(sessionKey(ctx))));
      taskBySession.delete(sessionKey(ctx));
    });

    api.on("session_end", async (_event, ctx) => {
      taskBySession.delete(sessionKey(ctx));
    });

    async function emitEvent(event) {
      const result = await relayBridgeEvent(event, config.bridgeUrl);
      if (!result.ok) {
        api.logger?.warn?.(`[openclaw-pet-bridge] queued event after send failure: ${result.error}`);
      }
    }
  }
};

export default plugin;

export function buildOpenClawAgentEvent(event, ctx = {}, config = {}, rememberedTask = "") {
  const task = rememberedTask || resolveTask(event.prompt, ctx, config);
  return {
    id: randomUUID(),
    time: new Date().toISOString(),
    source: config.source || "openclaw",
    type: "before_agent_start",
    status: "thinking",
    message: "OpenClaw is thinking",
    task,
    workspace: stringValue(ctx.workspaceDir),
    sessionId: stringValue(ctx.sessionId) || stringValue(ctx.sessionKey),
    notify: false
  };
}

export function buildOpenClawToolEvent(event, ctx = {}, config = {}, rememberedTask = "") {
  const tool = stringValue(event.toolName);
  return {
    id: randomUUID(),
    time: new Date().toISOString(),
    source: config.source || "openclaw",
    type: "before_tool_call",
    status: toolStatusForName(tool),
    message: tool ? `OpenClaw: ${tool}` : "OpenClaw is using a tool",
    task: rememberedTask || resolveTask("", ctx, config),
    workspace: stringValue(ctx.workspaceDir),
    sessionId: stringValue(ctx.sessionId) || stringValue(ctx.sessionKey),
    tool,
    notify: false
  };
}

export function buildOpenClawToolResultEvent(event, ctx = {}, config = {}, rememberedTask = "") {
  const approvalPending = isApprovalPending(event);
  return {
    id: randomUUID(),
    time: new Date().toISOString(),
    source: config.source || "openclaw",
    type: "after_tool_call",
    status: approvalPending ? "needs-attention" : "thinking",
    message: approvalPending ? "OpenClaw is waiting on approval" : "OpenClaw is thinking",
    task: rememberedTask || resolveTask("", ctx, config),
    workspace: stringValue(ctx.workspaceDir),
    sessionId: stringValue(ctx.sessionId) || stringValue(ctx.sessionKey),
    tool: stringValue(event.toolName),
    notify: approvalPending
  };
}

export function buildOpenClawAgentEndEvent(event, ctx = {}, config = {}, rememberedTask = "") {
  const status = event.success ? "completed" : "error";
  const notify = event.success ? config.notifyOnCompleted !== false : config.notifyOnError !== false;
  return {
    id: randomUUID(),
    time: new Date().toISOString(),
    source: config.source || "openclaw",
    type: "agent_end",
    status,
    message: event.success ? "OpenClaw finished the turn" : stringValue(event.error) || "OpenClaw ended with an error",
    task: rememberedTask || resolveTask("", ctx, config),
    workspace: stringValue(ctx.workspaceDir),
    sessionId: stringValue(ctx.sessionId) || stringValue(ctx.sessionKey),
    notify
  };
}

export function normalizePluginConfig(pluginConfig = {}) {
  return {
    bridgeUrl: stringValue(pluginConfig.bridgeUrl) || DEFAULT_BRIDGE_URL,
    source: stringValue(pluginConfig.source) || process.env.PET_OPENCLAW_SOURCE || "openclaw",
    taskPrefix: stringValue(pluginConfig.taskPrefix) || process.env.PET_OPENCLAW_TASK_PREFIX || "openclaw",
    notifyOnCompleted: pluginConfig.notifyOnCompleted !== false,
    notifyOnError: pluginConfig.notifyOnError !== false
  };
}

function resolveTask(prompt, ctx, config) {
  const promptSummary = summarizeTask(prompt);
  if (promptSummary) return promptSummary;
  return summarizeTask(ctx.sessionKey, ctx.sessionId, ctx.agentId) || `${config.taskPrefix || "openclaw"}-session`;
}

function sessionKey(ctx) {
  return stringValue(ctx.sessionKey) || stringValue(ctx.sessionId) || stringValue(ctx.agentId) || "openclaw-session";
}

function isApprovalPending(event) {
  const result = event?.result;
  const status = stringValue(result?.status) || stringValue(result?.state) || stringValue(event?.error);
  return status === "approval-pending" || status === "needs-approval";
}

function toolStatusForName(value) {
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

function summarizeTask(value) {
  if (typeof value !== "string") return "";
  const line = value.split(/\r?\n/).map((item) => item.trim()).find(Boolean) || "";
  if (!line) return "";
  return line.length > 72 ? `${line.slice(0, 71)}…` : line;
}

function stringValue(value) {
  return typeof value === "string" ? value.trim() : "";
}

async function relayBridgeEvent(event, bridgeUrl) {
  const result = await postEvent(event, bridgeUrl);
  if (!result.ok) {
    await enqueueEvent(event, result.error);
  }
  return result;
}

async function postEvent(event, bridgeUrl) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), DEFAULT_TIMEOUT_MS);
  try {
    const headers = { "content-type": "application/json" };
    const token = bridgeToken();
    if (token) headers.authorization = `Bearer ${token}`;
    const response = await fetch(bridgeUrl || DEFAULT_BRIDGE_URL, {
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

// Same lookup as src/token-file.js, inlined so the plugin has no relative imports.
function bridgeToken() {
  const direct = String(process.env.PET_BRIDGE_TOKEN || "").trim();
  if (direct) return direct;
  const file = String(process.env.PET_BRIDGE_TOKEN_FILE || "").trim() || join(homedir(), ".codex-pet-bridge", "token");
  try {
    return readFileSync(file, "utf8").trim();
  } catch {
    return "";
  }
}

async function enqueueEvent(event, reason) {
  let records = [];
  try {
    records = (await readFile(DEFAULT_QUEUE_PATH, "utf8"))
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
  await mkdir(dirname(DEFAULT_QUEUE_PATH), { recursive: true });
  const tmpPath = `${DEFAULT_QUEUE_PATH}.${process.pid}.${Date.now()}.${Math.random().toString(16).slice(2)}.tmp`;
  await writeFile(tmpPath, `${records.slice(-MAX_QUEUE).map((item) => JSON.stringify(item)).join("\n")}\n`, "utf8");
  await rename(tmpPath, DEFAULT_QUEUE_PATH);
}

function positiveNumber(value, fallback) {
  const parsed = Number(value);
  return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback;
}
