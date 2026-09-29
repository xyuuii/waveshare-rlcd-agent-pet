#!/usr/bin/env node
import http from "node:http";
import { appendFile, mkdir, readFile, rename, writeFile } from "node:fs/promises";
import { execFile } from "node:child_process";
import { randomUUID } from "node:crypto";
import { homedir, hostname as osHostname } from "node:os";
import { dirname, join, resolve } from "node:path";
import { promisify } from "node:util";

import { fileURLToPath } from "node:url";

import { AnimStore } from "./anim-store.js";
import {
  DEFAULT_CLAUDE_PROJECTS_DIR,
  claudeTokensToday,
  loadClaudeRealtimeState,
  shortModelName
} from "./claude-session-status.js";
import { inferCodexRealtimeStateFromLines, pickAutoCurrent } from "./codex-session-status.js";
import { buildVisibleAgentSlots, pruneAndCapSlots, resolveFocusSelection } from "./device-focus.js";
import { DeviceRegistry, parseTelemetry } from "./device-registry.js";
import { ANIM_ID_PATTERN, SettingsStore, displayCommand } from "./display-settings.js";
import { EggController } from "./egg-controller.js";
import { readSystemTimezone } from "./system-timezone.js";
import { resolveBridgeToken } from "./token-file.js";

const PORT = numberFromEnv("PET_BRIDGE_PORT", 17366);
const HOST = process.env.PET_BRIDGE_HOST || "127.0.0.1";
const LOG_PATH = resolve(process.env.PET_BRIDGE_LOG || "./events.jsonl");
const WEBHOOK_URL = process.env.PET_WEBHOOK_URL || "";
const WEBHOOK_TOKEN = process.env.PET_WEBHOOK_TOKEN || "";
const XIAOZHI_ASSISTANT_URL = stripTrailingSlash(process.env.XIAOZHI_ASSISTANT_URL || "");
const XIAOZHI_SOURCE_PREFIX = process.env.XIAOZHI_SOURCE_PREFIX || "";
const XIAOZHI_WEBHOOK_TOKEN = process.env.XIAOZHI_WEBHOOK_TOKEN || "";
const INBOUND_TOKEN = resolveBridgeToken({ allowDefaultFile: false });
const MAX_EVENTS = numberFromEnv("PET_BRIDGE_MAX_EVENTS", 200);
const MAX_NOTIFICATIONS = numberFromEnv("PET_BRIDGE_MAX_NOTIFICATIONS", 100);
const MAX_BODY_BYTES = numberFromEnv("PET_BRIDGE_MAX_BODY_BYTES", 65536);
const MAX_MESSAGE_CHARS = numberFromEnv("PET_BRIDGE_MAX_MESSAGE_CHARS", 500);
const STATE_PATH = resolve(process.env.PET_BRIDGE_STATE || "./bridge-state.json");
const OUTBOX_MAX = numberFromEnv("PET_BRIDGE_OUTBOX_MAX", 300);
const OUTBOX_FLUSH_MAX = numberFromEnv("PET_BRIDGE_OUTBOX_FLUSH_MAX", 25);
const OUTBOX_FLUSH_INTERVAL_MS = numberFromEnv("PET_BRIDGE_OUTBOX_FLUSH_INTERVAL_MS", 30000);
const SINK_TIMEOUT_MS = numberFromEnv("PET_BRIDGE_SINK_TIMEOUT_MS", numberFromEnv("XIAOZHI_WEBHOOK_TIMEOUT_MS", 1200));
const STORE_RAW_EVENTS = process.env.PET_BRIDGE_STORE_RAW === "1";
const NOTIFY_STATUSES = new Set(
  (process.env.PET_NOTIFY_STATUSES || "needs-attention,completed,near-complete,error")
    .split(",")
    .map((value) => value.trim())
    .filter(Boolean)
);
const NOTIFY_THROTTLE_MS = numberFromEnv("PET_NOTIFY_THROTTLE_MS", 15000);
const NOTIFICATION_WEBHOOK_URL = process.env.PET_NOTIFICATION_WEBHOOK_URL || "";
const CODEX_STATE_DB = resolve(process.env.PET_BRIDGE_CODEX_STATE || join(homedir(), ".codex", "state_5.sqlite"));
const CODEX_THREAD_CWD = process.env.PET_BRIDGE_CODEX_CWD || "";
const CODEX_SESSIONS_ROOT = resolve(process.env.PET_BRIDGE_CODEX_SESSIONS || join(homedir(), ".codex", "sessions"));
const USAGE_CACHE_MS = numberFromEnv("PET_BRIDGE_USAGE_CACHE_MS", 10000);
const CODEX_REALTIME_ACTIVE_MS = numberFromEnv("PET_BRIDGE_CODEX_REALTIME_ACTIVE_MS", 120000);
const CODEX_COMPLETED_HOLD_MS = numberFromEnv("PET_BRIDGE_CODEX_COMPLETED_HOLD_MS", 20000);
const CODEX_SOURCE_PREFIX = (process.env.PET_AGENT_SYNC_PREFIX || "local").trim();
const HERE = dirname(fileURLToPath(import.meta.url));
const SETTINGS_PATH = resolve(process.env.PET_BRIDGE_SETTINGS || join(dirname(STATE_PATH), "display-settings.json"));
const ANIM_DIR = resolve(process.env.PET_BRIDGE_ANIM_DIR || join(dirname(STATE_PATH), "anim"));
const UI_DIR = resolve(process.env.PET_BRIDGE_UI_DIR || join(HERE, "..", "ui"));
const MAX_UPLOAD_BYTES = numberFromEnv("PET_BRIDGE_MAX_UPLOAD_BYTES", 48 * 1024 * 1024);
const CLAUDE_PROJECTS_DIR = resolve(process.env.PET_BRIDGE_CLAUDE_PROJECTS || DEFAULT_CLAUDE_PROJECTS_DIR);
const CLAUDE_REALTIME_ENABLED = process.env.PET_BRIDGE_CLAUDE_TRANSCRIPTS !== "0";
const MAX_DEVICE_SLOTS = numberFromEnv("PET_BRIDGE_MAX_DEVICE_SLOTS", 6);
const SLOT_MAX_AGE_MS = numberFromEnv("PET_BRIDGE_SLOT_MAX_AGE_MS", 12 * 60 * 60 * 1000);
const EGG_DELAY_MS = numberFromEnv("PET_BRIDGE_EGG_DELAY_MS", 3500);
const BRIDGE_VERSION = await readPackageVersion();
const STARTED_AT = Date.now();

const recentEvents = [];
const recentNotifications = [];
const sinkOutbox = [];
const notificationThrottle = new Map();
const sseClients = new Set();
const deliveryState = {
  lastError: "",
  lastSuccessAt: ""
};
let flushPromise = null;
let persistPromise = Promise.resolve();
let usageCache = {
  loadedAt: 0,
  data: null
};
let claudeCache = { loadedAt: 0, realtime: null, today: 0, todayLoadedAt: 0 };
const settingsStore = new SettingsStore(SETTINGS_PATH);
const deviceRegistry = new DeviceRegistry();
const animStore = new AnimStore(ANIM_DIR, { maxBytes: MAX_UPLOAD_BYTES });
const eggController = new EggController();
const execFileAsync = promisify(execFile);

if (!isLoopbackHost(HOST) && !INBOUND_TOKEN && process.env.PET_BRIDGE_ALLOW_UNAUTH_REMOTE !== "1") {
  console.error("Refusing to listen on a non-loopback host without PET_BRIDGE_TOKEN.");
  console.error("Use SSH tunneling, set PET_BRIDGE_TOKEN, or set PET_BRIDGE_ALLOW_UNAUTH_REMOTE=1 for a trusted lab network.");
  process.exit(1);
}

await loadPersistentState();
await settingsStore.load();

const server = http.createServer(async (req, res) => {
  try {
    setCors(res);

    if (req.method === "OPTIONS") {
      res.writeHead(204);
      res.end();
      return;
    }

    const url = new URL(req.url || "/", `http://${req.headers.host || `${HOST}:${PORT}`}`);
    // The dashboard's static files carry no data; every API call below still needs the token.
    if (req.method === "GET" && (url.pathname === "/" || url.pathname === "/ui")) {
      res.writeHead(302, { location: "/ui/" });
      res.end();
      return;
    }
    if (req.method === "GET" && url.pathname.startsWith("/ui/")) {
      await serveUiFile(url.pathname, res);
      return;
    }
    if (PRIVATE_API.test(url.pathname)) {
      // Dashboard and menu bar API: never readable by other websites, and
      // requests must name this machine (blocks DNS rebinding).
      res.removeHeader("Access-Control-Allow-Origin");
      if (!isTrustedHost(req.headers.host) || !isSameOriginOrNone(req)) {
        sendJson(res, 403, { ok: false, error: "forbidden" });
        return;
      }
    }
    if (!isAuthorized(req, url)) {
      sendJson(res, 401, { ok: false, error: "unauthorized" });
      return;
    }

    if (req.method === "GET" && url.pathname === "/health") {
      sendJson(res, 200, {
        ok: true,
        service: "codex-pet-bridge",
        events: recentEvents.length,
        notifications: unreadNotifications().length,
        outboxDepth: sinkOutbox.length,
        lastDeliveryError: deliveryState.lastError || null,
        lastDeliverySuccessAt: deliveryState.lastSuccessAt || null
      });
      return;
    }

    if (req.method === "GET" && url.pathname === "/state") {
      sendJson(res, 200, {
        ok: true,
        current: recentEvents.at(-1) || null,
        latestNotification: unreadNotifications().at(0) || null,
        unreadCount: unreadNotifications().length
      });
      return;
    }

    if (req.method === "GET" && url.pathname === "/events") {
      sendJson(res, 200, { ok: true, events: recentEvents });
      return;
    }

    if (req.method === "GET" && url.pathname === "/notifications") {
      const includeRead = url.searchParams.get("include_read") === "1";
      const limit = numberFromValue(url.searchParams.get("limit"), MAX_NOTIFICATIONS);
      const notifications = (includeRead ? recentNotifications : unreadNotifications()).slice(0, limit);
      sendJson(res, 200, { ok: true, unreadCount: unreadNotifications().length, notifications });
      return;
    }

    if (req.method === "GET" && url.pathname === "/notifications/next") {
      sendJson(res, 200, { ok: true, notification: unreadNotifications().at(0) || null });
      return;
    }

    if (req.method === "GET" && url.pathname === "/esp32/poll") {
      const ackId = url.searchParams.get("ack");
      if (ackId) await ackNotification(ackId);
      const focus = stringValue(url.searchParams.get("focus")) || "auto";
      const { eggRequested } = deviceRegistry.update(parseTelemetry(url.searchParams), {
        address: clientAddress(req)
      });
      if (eggRequested) await playDefaultEgg("board");
      sendJson(res, 200, { ...(await compactDeviceState({ focus })), ...boardCommands() });
      return;
    }

    const chunkMatch = url.pathname.match(/^\/esp32\/anim\/([^/]+)\/frames$/);
    if (req.method === "GET" && chunkMatch) {
      const id = decodeURIComponent(chunkMatch[1]);
      const start = Math.max(0, Math.trunc(numberFromValue(url.searchParams.get("start"), 0)));
      const count = Math.max(1, Math.min(1000, Math.trunc(numberFromValue(url.searchParams.get("count"), 60))));
      const maxBytes = Math.max(1024, Math.min(65536, Math.trunc(numberFromValue(url.searchParams.get("max_bytes"), 16384))));
      const chunk = await animStore.chunk(id, start, count, maxBytes);
      res.writeHead(200, {
        "content-type": "application/octet-stream",
        "content-length": chunk.bytes.length,
        "x-rla-start": String(chunk.start),
        "x-rla-count": String(chunk.count),
        "cache-control": "no-store"
      });
      res.end(Buffer.from(chunk.bytes.buffer, chunk.bytes.byteOffset, chunk.bytes.length));
      return;
    }

    if (req.method === "GET" && url.pathname === "/status") {
      sendJson(res, 200, await statusView());
      return;
    }

    if (req.method === "GET" && url.pathname === "/settings") {
      sendJson(res, 200, { ok: true, settings: settingsStore.get(), timezone: timezoneView() });
      return;
    }

    if (req.method === "POST" && url.pathname === "/settings") {
      const patch = await readJson(req);
      const result = await settingsStore.update(patch);
      sendJson(res, result.errors.length ? 400 : 200, {
        ok: result.errors.length === 0,
        errors: result.errors,
        settings: result.settings
      });
      return;
    }

    if (req.method === "GET" && url.pathname === "/anim") {
      sendJson(res, 200, { ok: true, animations: await animStore.list(), maxBytes: MAX_UPLOAD_BYTES });
      return;
    }

    const animMatch = url.pathname.match(/^\/anim\/([^/]+)$/);
    if (animMatch && ["GET", "PUT", "DELETE"].includes(req.method)) {
      const id = decodeURIComponent(animMatch[1]);
      if (!ANIM_ID_PATTERN.test(id)) {
        sendJson(res, 400, { ok: false, error: "invalid animation id" });
        return;
      }
      if (req.method === "GET") {
        const { bytes } = await animStore.load(id);
        res.writeHead(200, {
          "content-type": "application/octet-stream",
          "content-length": bytes.length,
          "cache-control": "no-store"
        });
        res.end(Buffer.from(bytes.buffer, bytes.byteOffset, bytes.length));
        return;
      }
      if (req.method === "DELETE") {
        await animStore.remove(id);
        sendJson(res, 200, { ok: true });
        return;
      }
      const body = await readRawBody(req, MAX_UPLOAD_BYTES);
      const animation = await animStore.put(id, new Uint8Array(body.buffer, body.byteOffset, body.length));
      sendJson(res, 201, { ok: true, animation });
      return;
    }

    if (req.method === "POST" && url.pathname === "/egg/play") {
      const body = await readJson(req);
      const id = stringValue(body.id) || settingsStore.get().defaultAnimation;
      if (!id) {
        sendJson(res, 400, { ok: false, error: "no animation id and no default animation" });
        return;
      }
      const meta = await animStore.describe(id);
      const delayMs = numberFromValue(body.delayMs, EGG_DELAY_MS);
      sendJson(res, 200, { ok: true, serverTimeMs: Date.now(), egg: eggController.play(meta, { delayMs }) });
      return;
    }

    if (req.method === "POST" && url.pathname === "/egg/stop") {
      sendJson(res, 200, { ok: true, egg: eggController.stop() });
      return;
    }

    if (req.method === "GET" && url.pathname === "/stream") {
      openEventStream(req, res);
      return;
    }

    if (req.method === "POST" && url.pathname === "/events") {
      const body = await readJson(req);
      const event = normalizeEvent(body);
      await publishEvent(event);
      sendJson(res, 202, { ok: true, event });
      return;
    }

    const ackMatch = url.pathname.match(/^\/notifications\/([^/]+)\/ack$/);
    if (req.method === "POST" && ackMatch) {
      const notification = await ackNotification(ackMatch[1]);
      sendJson(res, notification ? 200 : 404, { ok: Boolean(notification), notification });
      return;
    }

    if (req.method === "POST" && url.pathname === "/notifications/ack-all") {
      const count = await ackAllNotifications();
      sendJson(res, 200, { ok: true, count });
      return;
    }

    sendJson(res, 404, { ok: false, error: "not_found" });
  } catch (error) {
    const status = Number.isInteger(error?.status) ? error.status : 500;
    sendJson(res, status, { ok: false, error: error.message || String(error) });
  }
});

server.listen(PORT, HOST, () => {
  console.log(`codex-pet-bridge listening on http://${HOST}:${PORT}`);
  console.log(`event log: ${LOG_PATH}`);
  if (WEBHOOK_URL) {
    console.log(`webhook sink enabled: ${WEBHOOK_URL}`);
  }
  if (NOTIFICATION_WEBHOOK_URL) {
    console.log(`notification webhook enabled: ${NOTIFICATION_WEBHOOK_URL}`);
  }
  if (XIAOZHI_ASSISTANT_URL) {
    console.log(`xiaozhi assistant sink enabled: ${XIAOZHI_ASSISTANT_URL}`);
  }
  if (sinkOutbox.length) {
    console.log(`sink outbox loaded: ${sinkOutbox.length} pending delivery records`);
  }
});

setInterval(() => {
  void flushSinkOutbox();
}, OUTBOX_FLUSH_INTERVAL_MS).unref();

void flushSinkOutbox();

function normalizeEvent(input = {}) {
  const now = new Date().toISOString();
  const raw = typeof input === "object" && input !== null ? input : { value: input };
  const source = stringValue(raw.source) || inferSource(raw) || "unknown";
  const type = stringValue(raw.type) || stringValue(raw.hook_event_name) || "status";
  const status = stringValue(raw.status) || mapHookStatus(raw);
  const message = truncate(stringValue(raw.message) || summarize(raw), MAX_MESSAGE_CHARS);

  const event = {
    id: stringValue(raw.id) || randomUUID(),
    time: stringValue(raw.time) || now,
    source,
    type,
    status,
    message,
    task: stringValue(raw.task) || stringValue(raw.codexTask) || "",
    notify: typeof raw.notify === "boolean" ? raw.notify : undefined,
    progress: numberFromValue(raw.progress, null),
    workspace: stringValue(raw.workspace) || stringValue(raw.cwd) || "",
    sessionId: stringValue(raw.sessionId) || stringValue(raw.session_id) || "",
    tool: stringValue(raw.tool) || stringValue(raw.tool_name) || ""
  };
  if (STORE_RAW_EVENTS) event.raw = redactRaw(raw);
  return event;
}

function inferSource(raw) {
  if (raw.hook_event_name || raw.session_id || raw.transcript_path) {
    return "claude-code";
  }
  if (raw.codexThreadId || raw.codex_thread_id) {
    return "codex";
  }
  return "";
}

function mapHookStatus(raw) {
  const name = stringValue(raw.hook_event_name);
  if (name === "Notification") return "needs-attention";
  if (name === "PreToolUse" || name === "PostToolUse") return toolStatusForName(raw.tool_name || raw.tool);
  if (name === "Stop") return "completed";
  if (name === "SessionStart") return "started";
  if (name === "UserPromptSubmit") return "thinking";
  return "event";
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

function summarize(raw) {
  if (raw.notification?.message) return String(raw.notification.message);
  if (raw.prompt) return `Prompt submitted: ${truncate(raw.prompt, 80)}`;
  if (raw.tool_name) return `${raw.hook_event_name || "Tool"}: ${raw.tool_name}`;
  if (raw.hook_event_name) return raw.hook_event_name;
  return "Agent event received";
}

async function publishEvent(event) {
  recentEvents.push(event);
  while (recentEvents.length > MAX_EVENTS) recentEvents.shift();

  await mkdir(dirname(LOG_PATH), { recursive: true });
  await appendFile(LOG_PATH, `${JSON.stringify(event)}\n`, "utf8");

  broadcastSse(event);

  const notification = createNotification(event);
  if (notification) {
    recentNotifications.unshift(notification);
    while (recentNotifications.length > MAX_NOTIFICATIONS) recentNotifications.pop();
    broadcastSse(notification, "notification");
  }

  await persistState();
  void flushSinkOutbox();
  void postWebhook(event, "webhook:event");
  void postXiaozhiNotification(event);
  if (notification) {
    void postWebhook(notification, "webhook:notification");
  }
}

function openEventStream(req, res) {
  res.writeHead(200, {
    "Content-Type": "text/event-stream",
    "Cache-Control": "no-cache, no-transform",
    Connection: "keep-alive",
    "X-Accel-Buffering": "no"
  });
  res.write(": connected\n\n");

  for (const event of recentEvents.slice(-20)) {
    writeSse(res, event);
  }
  for (const notification of recentNotifications.slice(0, 5).reverse()) {
    writeSse(res, notification, "notification");
  }

  sseClients.add(res);
  req.on("close", () => sseClients.delete(res));
}

function broadcastSse(event, eventName) {
  for (const client of sseClients) {
    writeSse(client, event, eventName);
  }
}

function writeSse(res, event, eventName = "message") {
  res.write(`id: ${event.id}\n`);
  res.write(`event: ${eventName}\n`);
  res.write(`data: ${JSON.stringify(event)}\n\n`);
  if (eventName !== "message") {
    res.write(`id: ${event.id}\n`);
    res.write(`data: ${JSON.stringify(event)}\n\n`);
  }
}

function createNotification(event) {
  if (event.notify === false) return null;
  if (event.notify !== true && !NOTIFY_STATUSES.has(event.status)) return null;

  const key = notificationKey(event);
  const now = Date.now();
  const lastSent = notificationThrottle.get(key) || 0;
  if (now - lastSent < NOTIFY_THROTTLE_MS) return null;
  notificationThrottle.set(key, now);

  return {
    id: randomUUID(),
    eventId: event.id,
    time: event.time,
    kind: "notification",
    source: event.source,
    task: taskForEvent(event),
    status: event.status,
    priority: priorityForStatus(event.status),
    title: titleForStatus(event.status),
    message: event.message,
    workspace: event.workspace,
    sessionId: event.sessionId,
    progress: event.progress,
    read: false
  };
}

function notificationKey(event) {
  return [event.source, taskForEvent(event), event.sessionId, event.workspace, event.status, event.message].join("|");
}

function titleForStatus(status) {
  if (status === "needs-attention") return "Waiting for you";
  if (status === "completed") return "Task completed";
  if (status === "near-complete") return "Almost done";
  if (status === "error") return "Needs review";
  return "Agent update";
}

function priorityForStatus(status) {
  if (status === "error") return 3;
  if (status === "needs-attention") return 2;
  return 1;
}

function unreadNotifications() {
  return recentNotifications.filter((notification) => !notification.read);
}

async function ackNotification(id) {
  const notification = recentNotifications.find((item) => item.id === id);
  if (!notification) return null;
  notification.read = true;
  notification.readAt = new Date().toISOString();
  await persistState();
  await postXiaozhiClear(notification);
  return notification;
}

async function ackAllNotifications() {
  const now = new Date().toISOString();
  let count = 0;
  for (const notification of recentNotifications) {
    if (!notification.read) {
      notification.read = true;
      notification.readAt = now;
      await postXiaozhiClear(notification);
      count += 1;
    }
  }
  await persistState();
  return count;
}

// The board, the dashboard and the menu bar app all poll; one view per focus
// per 750 ms keeps sqlite/tail calls down without making the board feel late.
let agentViewCache = { key: "", at: 0, value: null };

async function agentView(options = {}) {
  const key = `${options.focus || "auto"}|${recentEvents.length}|${recentEvents.at(-1)?.id || ""}`;
  if (agentViewCache.key === key && Date.now() - agentViewCache.at < 750) return agentViewCache.value;
  const value = await buildAgentView(options);
  agentViewCache = { key, at: Date.now(), value };
  return value;
}

async function buildAgentView(options = {}) {
  const eventCurrent = recentEvents.at(-1) || null;
  const latestCodexEvent = [...recentEvents].reverse().find((item) => sourceFamily(item.source) === "codex") || null;
  const codexRealtime = await loadCodexRealtimeState(latestCodexEvent);
  const claudeRealtime = await loadClaudeRealtimeCached();
  const realtimes = [codexRealtime, claudeRealtime].filter(Boolean);
  const autoCurrent = pickAutoCurrent(eventCurrent, realtimes);
  const visibleSlots = pruneAndCapSlots(buildVisibleAgentSlots(recentEvents, realtimes), {
    maxSlots: MAX_DEVICE_SLOTS,
    maxAgeMs: SLOT_MAX_AGE_MS
  });
  const selected = resolveFocusSelection({
    focus: options.focus,
    slots: visibleSlots,
    autoCurrent: slotStateFromCurrent(autoCurrent)
  });
  const current = materializeCurrentState(selected, autoCurrent);
  const usage = await loadUsageSummary(current, claudeRealtime);
  return { selected, current, visibleSlots, usage, codexRealtime, claudeRealtime };
}

async function compactDeviceState(options = {}) {
  const notification = unreadNotifications().at(0) || null;
  const { selected, current, visibleSlots, usage } = await agentView(options);
  return {
    ok: true,
    unread_count: unreadNotifications().length,
    source: current?.source || "",
    task: current?.task || "",
    updated_at: current?.time || "",
    current_status: current?.status || "idle",
    focus_mode: selected.mode,
    focus_id: selected.focusId,
    focus_index: selected.focusIndex,
    focus_count: selected.focusCount,
    agents: visibleSlots.map(compactAgentSlot),
    usage,
    notification: notification ? compactNotification(notification) : null
  };
}

function compactAgentSlot(slot) {
  return {
    id: slot.id,
    source: slot.source,
    task: slot.task,
    status: slot.status,
    updated_at: slot.updated_at
  };
}

function compactNotification(notification) {
  return {
    id: notification.id,
    source: notification.source,
    task: notification.task,
    status: notification.status,
    priority: notification.priority,
    title: notification.title,
    message: notification.message,
    project: notification.workspace,
    time: notification.time
  };
}

function slotStateFromCurrent(current) {
  if (!current) return null;
  return {
    source: current.source || "",
    task: current.task || "",
    status: current.status || "idle",
    updated_at: current.time || "",
    sessionId: current.sessionId || "",
    workspace: current.workspace || ""
  };
}

function materializeCurrentState(selected, autoCurrent) {
  if (selected.mode !== "pinned") {
    return autoCurrent;
  }
  const current = selected.current;
  if (!current) return autoCurrent;
  return {
    source: current.source || "",
    task: current.task || "",
    status: current.status || "idle",
    time: current.updated_at || "",
    sessionId: current.sessionId || "",
    workspace: current.workspace || ""
  };
}

async function loadUsageSummary(current, claudeRealtime = null) {
  if (!current?.source || sourceFamily(current.source) === "codex") {
    return await loadCodexUsageSummary();
  }
  if (sourceFamily(current.source) === "claude" && CLAUDE_REALTIME_ENABLED) {
    return await loadClaudeUsageSummary(claudeRealtime);
  }
  return buildLocalActivityUsage(current);
}

async function loadClaudeRealtimeCached() {
  if (!CLAUDE_REALTIME_ENABLED) return null;
  if (Date.now() - claudeCache.loadedAt < 1500) return claudeCache.realtime;
  let realtime = null;
  try {
    realtime = await loadClaudeRealtimeState({ projectsDir: CLAUDE_PROJECTS_DIR, sourcePrefix: CODEX_SOURCE_PREFIX });
  } catch {
    realtime = null;
  }
  claudeCache = { ...claudeCache, loadedAt: Date.now(), realtime };
  return realtime;
}

async function loadClaudeUsageSummary(claudeRealtime) {
  if (Date.now() - claudeCache.todayLoadedAt > USAGE_CACHE_MS) {
    try {
      claudeCache.today = await claudeTokensToday({ projectsDir: CLAUDE_PROJECTS_DIR });
    } catch {
      claudeCache.today = 0;
    }
    claudeCache.todayLoadedAt = Date.now();
  }
  const realtime = claudeRealtime || claudeCache.realtime;
  const context = realtime?.context;
  const model = shortModelName(realtime?.model);
  return usageSummary({
    today: claudeCache.today > 0 ? `${abbrevNumber(claudeCache.today)} tok` : "--",
    context: context ? `${abbrevNumber(context.used)} / ${abbrevNumber(context.window)}` : "--",
    quota: model || "--",
    todayLabel: "TODAY",
    todayHint: "Claude Code tokens today, no cache reads",
    contextLabel: "CONTEXT",
    contextHint: "Last turn context / model window",
    quotaLabel: "MODEL",
    quotaHint: "Plan limits are not exposed locally",
    quotaStyle: "text"
  });
}

function timezoneView() {
  const system = readSystemTimezone();
  const override = settingsStore.get().tzOverride;
  return { zone: system.zone, posix: system.posix, override, effective: override || system.posix };
}

// Extra instructions for the board, appended to /esp32/poll.
function boardCommands() {
  const commands = { server_time_ms: Date.now() };
  const display = displayCommand(settingsStore.get());
  if (display) commands.display = display;
  const tz = timezoneView();
  if (tz.effective) commands.time = { tz: tz.effective, zone: tz.zone };
  const egg = eggController.command();
  if (egg) commands.egg = egg;
  return commands;
}

async function playDefaultEgg(reason) {
  const preferred = settingsStore.get().defaultAnimation;
  const animations = await animStore.list();
  const meta = animations.find((item) => item.id === preferred) || animations[0];
  if (!meta) {
    console.log(`egg requested by ${reason}: no animation uploaded, the board plays its built-in one`);
    return null;
  }
  console.log(`egg requested by ${reason}: playing ${meta.id}`);
  return eggController.play(meta, { delayMs: EGG_DELAY_MS });
}

async function statusView() {
  const { selected, current, visibleSlots, usage, codexRealtime, claudeRealtime } = await agentView({ focus: "auto" });
  return {
    ok: true,
    serverTimeMs: Date.now(),
    bridge: {
      version: BRIDGE_VERSION,
      startedAt: new Date(STARTED_AT).toISOString(),
      uptimeS: Math.round((Date.now() - STARTED_AT) / 1000),
      host: HOST,
      port: PORT,
      tokenRequired: Boolean(INBOUND_TOKEN)
    },
    device: deviceRegistry.snapshot(),
    current: current
      ? { source: current.source || "", task: current.task || "", status: current.status || "idle", time: current.time || "" }
      : null,
    agents: visibleSlots.map((slot) => ({
      ...compactAgentSlot(slot),
      family: sourceFamily(slot.source),
      project: slugFromPath(slot.workspace)
    })),
    focusCount: selected.focusCount,
    usage,
    realtime: {
      codex: codexRealtime ? { status: codexRealtime.status, time: codexRealtime.time } : null,
      claude: claudeRealtime
        ? {
            status: claudeRealtime.status,
            time: claudeRealtime.time,
            tool: claudeRealtime.tool,
            model: claudeRealtime.model,
            context: claudeRealtime.context,
            project: slugFromPath(claudeRealtime.workspace)
          }
        : null
    },
    unreadCount: unreadNotifications().length,
    settings: settingsStore.get(),
    timezone: timezoneView(),
    animations: await animStore.list(),
    egg: eggController.state()
  };
}

async function loadCodexUsageSummary() {
  if (Date.now() - usageCache.loadedAt < USAGE_CACHE_MS) {
    return usageCache.data;
  }

  const latestRolloutPath = await latestTopLevelRolloutPath();
  if (!latestRolloutPath) {
    const empty = usageSummary({
      today: "--",
      context: "--",
      quota: "--",
      todayLabel: "TODAY",
      todayHint: "Today total in this workspace",
      contextLabel: "CONTEXT",
      contextHint: "Current turn tokens / model window",
      quotaLabel: "QUOTA",
      quotaHint: "Remaining 5-hour and weekly limits",
      quotaStyle: "quota"
    });
    usageCache = { loadedAt: Date.now(), data: empty };
    return empty;
  }

  const latestTokenPayload = await readLastTokenCountPayload(latestRolloutPath);
  if (!latestTokenPayload) {
    const empty = usageSummary({
      today: "--",
      context: "--",
      quota: "--",
      todayLabel: "TODAY",
      todayHint: "Today total in this workspace",
      contextLabel: "CONTEXT",
      contextHint: "Current turn tokens / model window",
      quotaLabel: "QUOTA",
      quotaHint: "Remaining 5-hour and weekly limits",
      quotaStyle: "quota"
    });
    usageCache = { loadedAt: Date.now(), data: empty };
    return empty;
  }

  const dailyTotalTokens = await sumTodayTopLevelTokens();
  const totalTokens = numberFromValue(latestTokenPayload.info?.total_token_usage?.total_tokens, 0);
  const lastTurnTokens = numberFromValue(latestTokenPayload.info?.last_token_usage?.total_tokens, 0);
  const contextWindow = numberFromValue(latestTokenPayload.info?.model_context_window, 0);
  const primaryUsed = numberFromValue(latestTokenPayload.rate_limits?.primary?.used_percent, null);
  const secondaryUsed = numberFromValue(latestTokenPayload.rate_limits?.secondary?.used_percent, null);

  const todayTokens = dailyTotalTokens || totalTokens;
  const usage = usageSummary({
    today: todayTokens > 0 ? `${abbrevNumber(todayTokens)} tok` : "--",
    context: contextWindow > 0 && lastTurnTokens > 0
      ? `${abbrevNumber(lastTurnTokens)} / ${abbrevNumber(contextWindow)}`
      : "--",
    quota: formatQuotaLabel(primaryUsed, secondaryUsed),
    todayLabel: "TODAY",
    todayHint: "Today total in this workspace",
    contextLabel: "CONTEXT",
    contextHint: "Current turn tokens / model window",
    quotaLabel: "QUOTA",
    quotaHint: "Remaining 5-hour and weekly limits",
    quotaStyle: "quota"
  });

  usageCache = { loadedAt: Date.now(), data: usage };
  return usage;
}

async function loadCodexRealtimeState(latestCodexEvent) {
  const rolloutPath = await latestTopLevelRolloutPath();
  if (!rolloutPath) return null;
  try {
    const { stdout } = await execFileAsync("tail", ["-n", "240", rolloutPath], { maxBuffer: 8 * 1024 * 1024 });
    const realtime = inferCodexRealtimeStateFromLines(stdout.split("\n"));
    if (!realtime?.status || !realtime?.time) return null;
    const ageMs = Date.now() - Date.parse(realtime.time);
    if (!Number.isFinite(ageMs) || ageMs < 0) return null;
    if (realtime.status === "completed") {
      if (ageMs > CODEX_COMPLETED_HOLD_MS) return null;
    } else if (ageMs > CODEX_REALTIME_ACTIVE_MS) {
      return null;
    }
    return {
      source: latestCodexEvent?.source || `${CODEX_SOURCE_PREFIX}-codex`,
      task: latestCodexEvent?.task || `${CODEX_SOURCE_PREFIX}-codex-runtime`,
      status: realtime.status,
      time: realtime.time,
      sessionId: latestCodexEvent?.sessionId || "",
      workspace: latestCodexEvent?.workspace || CODEX_THREAD_CWD || ""
    };
  } catch {
    return null;
  }
}

async function latestTopLevelRolloutPath() {
  const clauses = ["rollout_path is not null", "source not like '{%'"];
  if (CODEX_THREAD_CWD) {
    clauses.push(`cwd = '${sqlEscape(CODEX_THREAD_CWD)}'`);
  }
  const query = `select rollout_path from threads where ${clauses.join(" and ")} order by rowid desc limit 1;`;
  const rows = await runSqliteQuery(query);
  return rows[0]?.rollout_path || "";
}

async function sumTodayTopLevelTokens() {
  const now = new Date();
  const yyyy = String(now.getFullYear());
  const mm = String(now.getMonth() + 1).padStart(2, "0");
  const dd = String(now.getDate()).padStart(2, "0");
  const dayPrefix = join(CODEX_SESSIONS_ROOT, yyyy, mm, dd);
  const clauses = [`rollout_path like '${sqlEscape(dayPrefix)}/%'`, "source not like '{%'"];
  if (CODEX_THREAD_CWD) {
    clauses.push(`cwd = '${sqlEscape(CODEX_THREAD_CWD)}'`);
  }
  const query = `select rollout_path from threads where ${clauses.join(" and ")};`;
  const rows = await runSqliteQuery(query);
  let total = 0;
  for (const row of rows) {
    const payload = await readLastTokenCountPayload(row.rollout_path);
    total += numberFromValue(payload?.info?.total_token_usage?.total_tokens, 0);
  }
  return total;
}

async function readLastTokenCountPayload(rolloutPath) {
  if (!rolloutPath) return null;
  try {
    const { stdout } = await execFileAsync("tail", ["-n", "400", rolloutPath], { maxBuffer: 8 * 1024 * 1024 });
    const lines = stdout.split("\n").reverse();
    for (const line of lines) {
      if (!line.trim()) continue;
      try {
        const item = JSON.parse(line);
        if (item.type === "event_msg" && item.payload?.type === "token_count") {
          return item.payload;
        }
      } catch {
        // ignore malformed lines
      }
    }
  } catch {
    return null;
  }
  return null;
}

async function runSqliteQuery(query) {
  try {
    const { stdout } = await execFileAsync(
      "sqlite3",
      ["-readonly", "-json", CODEX_STATE_DB, query],
      { maxBuffer: 2 * 1024 * 1024 }
    );
    return JSON.parse(stdout || "[]");
  } catch {
    return [];
  }
}

function abbrevNumber(value) {
  const abs = Math.abs(value);
  if (abs >= 1_000_000) return `${(value / 1_000_000).toFixed(abs >= 10_000_000 ? 0 : 1)}M`;
  if (abs >= 1_000) return `${(value / 1_000).toFixed(abs >= 100_000 ? 0 : 1)}k`;
  return String(value);
}

function formatQuotaLabel(primaryUsed, secondaryUsed) {
  const parts = [];
  if (primaryUsed !== null) parts.push(`5H ${Math.max(0, 100 - Math.round(primaryUsed))}%`);
  if (secondaryUsed !== null) parts.push(`WK ${Math.max(0, 100 - Math.round(secondaryUsed))}%`);
  return parts.length ? parts.join(" ") : "--";
}

function usageSummary({
  today,
  context,
  quota,
  todayLabel,
  todayHint,
  contextLabel,
  contextHint,
  quotaLabel,
  quotaHint,
  quotaStyle
}) {
  return {
    today,
    today_label: todayLabel,
    today_hint: todayHint,
    context,
    context_label: contextLabel,
    context_hint: contextHint,
    quota,
    quota_label: quotaLabel,
    quota_hint: quotaHint,
    quota_style: quotaStyle
  };
}

function buildLocalActivityUsage(current) {
  const base = usageSummary({
    today: "--",
    context: "0 calls",
    quota: "--",
    todayLabel: "SESS",
    todayHint: "Elapsed active time",
    contextLabel: "TOOLS",
    contextHint: "Tool starts in this session",
    quotaLabel: "ATTN",
    quotaHint: "User approval / attention state",
    quotaStyle: "text"
  });
  if (!current?.source) {
    return base;
  }

  const matches = eventsForCurrentScope(current);
  const oldestMs = oldestScopeTimestamp(matches);
  if (oldestMs > 0) {
    base.today = formatElapsed(Date.now() - oldestMs);
  }

  const toolStarts = matches.filter(isToolStartEvent).length;
  base.context = formatToolCount(toolStarts);
  base.quota = attentionStateLabel(current, matches);
  return base;
}

function eventsForCurrentScope(current) {
  const currentFamily = sourceFamily(current?.source);
  return recentEvents.filter((event) => {
    if (sourceFamily(event.source) !== currentFamily) return false;
    if (current.source && event.source && event.source !== current.source) return false;
    if (!scopeIdentityMatches(current, event)) return false;
    if (current.task) return event.task === current.task;
    return true;
  });
}

function scopeIdentityMatches(current, item) {
  const currentHasStrongIdentity = Boolean(current?.sessionId || current?.workspace);
  const itemHasStrongIdentity = Boolean(item?.sessionId || item?.workspace);

  if (current?.sessionId && item?.sessionId && current.sessionId !== item.sessionId) {
    return false;
  }
  if (current?.workspace && item?.workspace && current.workspace !== item.workspace) {
    return false;
  }
  if (current?.sessionId && item?.sessionId) return true;
  if (current?.workspace && item?.workspace) return true;
  if (currentHasStrongIdentity && itemHasStrongIdentity) return false;
  return true;
}

function oldestScopeTimestamp(events) {
  let oldest = 0;
  for (const event of events) {
    const time = Date.parse(event.time || "");
    if (!Number.isFinite(time) || time <= 0) continue;
    if (!oldest || time < oldest) oldest = time;
  }
  return oldest;
}

function isToolStartEvent(event) {
  const type = String(event?.type || "").toLowerCase();
  if (type === "pretooluse" || type === "pre_tool_call" || type === "before_tool_call") {
    return true;
  }
  return false;
}

function formatToolCount(count) {
  return `${count} ${count === 1 ? "call" : "calls"}`;
}

function attentionStateLabel(current, matches) {
  if (String(current?.status || "") === "needs-attention") {
    return "WAITING";
  }
  if (["error", "failed", "fail", "blocked"].includes(String(current?.status || ""))) {
    return "CHECK";
  }

  const unread = unreadNotifications().some((notification) => notificationMatchesScope(notification, current, matches));
  return unread ? "WAITING" : "CLEAR";
}

function notificationMatchesScope(notification, current, matches) {
  if (sourceFamily(notification?.source) !== sourceFamily(current?.source)) return false;
  if (current?.source && notification?.source && notification.source !== current.source) return false;
  if (!scopeIdentityMatches(current, notification)) return false;
  if (current?.task) return notification.task === current.task;
  if (matches.length && notification?.task) {
    return matches.some((event) => event.task && event.task === notification.task);
  }
  return true;
}

function formatElapsed(durationMs) {
  if (!Number.isFinite(durationMs) || durationMs < 0) return "--";
  const totalSeconds = Math.floor(durationMs / 1000);
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  if (hours > 0) {
    return `${hours}h ${String(minutes).padStart(2, "0")}m`;
  }
  if (minutes > 0) {
    return `${minutes}m ${String(seconds).padStart(2, "0")}s`;
  }
  return `${seconds}s`;
}

function sqlEscape(value) {
  return String(value || "").replace(/'/g, "''");
}

async function postWebhook(event, kind) {
  await sendOrQueueSink(kind, event);
}

async function postXiaozhiNotification(event) {
  if (!XIAOZHI_ASSISTANT_URL) return;
  const payload = xiaozhiPayloadForEvent(event);
  if (!payload) return;
  await sendOrQueueSink("xiaozhi:event", payload);
}

async function postXiaozhiClear(notification) {
  if (!XIAOZHI_ASSISTANT_URL) return;
  const payload = {
    source: xiaozhiSourceFor(notification),
    task: notification.task || taskForEvent(notification),
    status: "clear",
    message: "Notification acknowledged",
    priority: priorityName(notification.priority),
    needs_user: false
  };
  await sendOrQueueSink("xiaozhi:clear", payload);
}

function xiaozhiPayloadForEvent(event) {
  const status = xiaozhiStatusFor(event.status);
  if (!status) return null;
  if (event.notify === false && status !== "running" && status !== "clear") return null;
  const needsUser = event.notify !== false && (
    event.notify === true || ["done", "error", "waiting_user", "blocked"].includes(status)
  );
  return {
    source: xiaozhiSourceFor(event),
    task: taskForEvent(event),
    status,
    message: event.message || titleForStatus(event.status),
    priority: priorityName(priorityForStatus(event.status)),
    needs_user: needsUser
  };
}

function xiaozhiStatusFor(status) {
  if (["thinking", "working", "searching", "tool-use", "started", "running", "progress", "near-complete"].includes(status)) return "running";
  if (["completed", "complete", "done", "success", "succeeded", "finished"].includes(status)) return "done";
  if (["needs-attention", "waiting", "waiting-user", "waiting_user"].includes(status)) return "waiting_user";
  if (["error", "failed", "fail", "blocked"].includes(status)) return "error";
  if (["idle", "clear", "ack", "dismissed"].includes(status)) return "clear";
  return null;
}

function xiaozhiSourceFor(event) {
  const family = sourceFamily(event.source);
  const prefix = XIAOZHI_SOURCE_PREFIX.trim().toLowerCase();
  return prefix && family ? `${prefix}-${family}` : event.source || "pet-bridge";
}

function sourceFamily(source) {
  const text = String(source || "").toLowerCase();
  if (text.includes("claude")) return "claude";
  if (text.includes("codex")) return "codex";
  if (text.includes("hermes")) return "hermes";
  if (text.includes("openclaw")) return "openclaw";
  return "";
}

function taskForEvent(event) {
  return event.task || event.sessionId || slugFromPath(event.workspace) || "agent-task";
}

function slugFromPath(value) {
  const text = String(value || "").trim();
  if (!text) return "";
  return text.split(/[\\/]/).filter(Boolean).at(-1) || "";
}

function priorityName(priority) {
  if (typeof priority === "string") return priority;
  if (priority >= 3) return "urgent";
  if (priority === 2) return "high";
  return "normal";
}

function xiaozhiHeaders() {
  const headers = { "content-type": "application/json" };
  if (XIAOZHI_WEBHOOK_TOKEN) headers.authorization = `Bearer ${XIAOZHI_WEBHOOK_TOKEN}`;
  return headers;
}

async function sendOrQueueSink(kind, payload) {
  const target = sinkTarget(kind);
  if (!target) return;
  const result = await postJsonWithTimeout(target.url, payload, target.headers);
  if (result.ok) {
    deliveryState.lastSuccessAt = new Date().toISOString();
    deliveryState.lastError = "";
    await persistState();
    return;
  }
  enqueueSink(kind, payload, result.error);
  await persistState();
}

async function flushSinkOutbox() {
  if (flushPromise) return flushPromise;
  flushPromise = doFlushSinkOutbox().finally(() => {
    flushPromise = null;
  });
  return flushPromise;
}

async function doFlushSinkOutbox() {
  if (!sinkOutbox.length) return;
  const pending = sinkOutbox.splice(0, sinkOutbox.length);
  const remaining = [];
  let delivered = 0;
  for (const item of pending) {
    if (delivered >= OUTBOX_FLUSH_MAX) {
      remaining.push(item);
      continue;
    }
    const target = sinkTarget(item.kind);
    if (!target) {
      remaining.push(item);
      continue;
    }
    const result = await postJsonWithTimeout(target.url, item.payload, target.headers);
    if (result.ok) {
      delivered += 1;
      deliveryState.lastSuccessAt = new Date().toISOString();
      deliveryState.lastError = "";
      continue;
    }
    remaining.push({
      ...item,
      attempts: Number(item.attempts || 0) + 1,
      lastError: result.error,
      lastAttemptAt: new Date().toISOString()
    });
  }
  const combined = [...remaining, ...sinkOutbox].slice(-OUTBOX_MAX);
  sinkOutbox.splice(0, sinkOutbox.length, ...combined);
  await persistState();
}

function enqueueSink(kind, payload, reason) {
  sinkOutbox.push({
    id: randomUUID(),
    kind,
    payload,
    attempts: 0,
    createdAt: new Date().toISOString(),
    lastError: reason
  });
  while (sinkOutbox.length > OUTBOX_MAX) sinkOutbox.shift();
  deliveryState.lastError = `${kind}: ${reason}`;
}

function sinkTarget(kind) {
  if (kind === "webhook:event") {
    if (!WEBHOOK_URL) return null;
    return { url: WEBHOOK_URL, headers: webhookHeaders(WEBHOOK_TOKEN) };
  }
  if (kind === "webhook:notification") {
    if (!NOTIFICATION_WEBHOOK_URL) return null;
    return { url: NOTIFICATION_WEBHOOK_URL, headers: webhookHeaders(WEBHOOK_TOKEN) };
  }
  if (kind === "xiaozhi:event" || kind === "xiaozhi:clear") {
    if (!XIAOZHI_ASSISTANT_URL) return null;
    return {
      url: `${XIAOZHI_ASSISTANT_URL}/assistant/notifications`,
      headers: xiaozhiHeaders()
    };
  }
  return null;
}

function webhookHeaders(token) {
  const headers = { "content-type": "application/json" };
  if (token) headers.authorization = `Bearer ${token}`;
  return headers;
}

async function postJsonWithTimeout(targetUrl, payload, headers) {
  try {
    const response = await fetch(targetUrl, {
      method: "POST",
      headers,
      body: JSON.stringify(payload),
      signal: AbortSignal.timeout(SINK_TIMEOUT_MS)
    });
    if (!response.ok) {
      const error = `${response.status} ${response.statusText}`;
      console.error(`sink failed: ${error}`);
      return { ok: false, error };
    }
    return { ok: true };
  } catch (error) {
    const message = error.message || String(error);
    console.error(`sink ignored error: ${message}`);
    return { ok: false, error: message };
  }
}

async function loadPersistentState() {
  try {
    const state = JSON.parse(await readFile(STATE_PATH, "utf8"));
    if (Array.isArray(state.notifications)) {
      recentNotifications.push(...state.notifications.slice(0, MAX_NOTIFICATIONS));
    }
    if (Array.isArray(state.outbox)) {
      sinkOutbox.push(...state.outbox.slice(-OUTBOX_MAX));
    }
    if (state.deliveryState && typeof state.deliveryState === "object") {
      deliveryState.lastError = stringValue(state.deliveryState.lastError);
      deliveryState.lastSuccessAt = stringValue(state.deliveryState.lastSuccessAt);
    }
  } catch {
    // Missing or unreadable state should not stop the bridge.
  }
}

function persistState() {
  persistPromise = persistPromise.catch(() => {}).then(writePersistentState);
  return persistPromise;
}

async function writePersistentState() {
  try {
    await mkdir(dirname(STATE_PATH), { recursive: true });
    const state = {
      version: 1,
      notifications: recentNotifications.slice(0, MAX_NOTIFICATIONS),
      outbox: sinkOutbox.slice(-OUTBOX_MAX),
      deliveryState
    };
    const tmpPath = `${STATE_PATH}.${process.pid}.${Date.now()}.${Math.random().toString(16).slice(2)}.tmp`;
    await writeFile(tmpPath, `${JSON.stringify(state, null, 2)}\n`, "utf8");
    await rename(tmpPath, STATE_PATH);
  } catch {
    // State persistence is best-effort; event ingestion remains available.
  }
}

async function readRawBody(req, maxBytes) {
  const declared = Number(req.headers["content-length"] || 0);
  if (declared > maxBytes) throw Object.assign(new Error("request body too large"), { status: 413 });
  const chunks = [];
  let size = 0;
  for await (const chunk of req) {
    size += chunk.length;
    if (size > maxBytes) throw Object.assign(new Error("request body too large"), { status: 413 });
    chunks.push(chunk);
  }
  return Buffer.concat(chunks);
}

const UI_TYPES = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".png": "image/png",
  ".svg": "image/svg+xml",
  ".json": "application/json"
};

async function serveUiFile(pathname, res) {
  let relative = decodeURIComponent(pathname.slice("/ui/".length)) || "index.html";
  if (relative.endsWith("/")) relative += "index.html";
  // The codec is shared with the server so the browser encodes exactly what the board decodes.
  const file = relative === "rla-codec.js" ? join(HERE, "rla-codec.js") : resolve(UI_DIR, relative);
  if (relative !== "rla-codec.js" && !file.startsWith(`${UI_DIR}/`)) {
    sendJson(res, 404, { ok: false, error: "not_found" });
    return;
  }
  try {
    const body = await readFile(file);
    const type = UI_TYPES[file.slice(file.lastIndexOf("."))] || "application/octet-stream";
    res.writeHead(200, {
      "content-type": type,
      "cache-control": "no-cache",
      "content-security-policy":
        "default-src 'self'; img-src 'self' data: blob:; media-src 'self' blob:; style-src 'self' 'unsafe-inline'; connect-src 'self'; script-src 'self'"
    });
    res.end(body);
  } catch {
    sendJson(res, 404, { ok: false, error: "not_found" });
  }
}

const PRIVATE_API = /^\/(status|settings|anim|egg)(\/|$)/;

function isTrustedHost(hostHeader) {
  let hostname = "";
  try {
    hostname = new URL(`http://${hostHeader || ""}`).hostname.toLowerCase();
  } catch {
    return false;
  }
  if (!hostname) return false;
  if (hostname.startsWith("[") || /^\d{1,3}(\.\d{1,3}){3}$/.test(hostname)) return true;
  if (hostname === "localhost" || hostname.endsWith(".localhost") || hostname.endsWith(".local")) return true;
  const machine = hostnameOfThisMac().toLowerCase();
  if (machine && (hostname === machine || hostname === `${machine.replace(/\.local$/, "")}.local`)) return true;
  return String(process.env.PET_BRIDGE_ALLOWED_HOSTS || "")
    .split(",")
    .map((item) => item.trim().toLowerCase())
    .filter(Boolean)
    .includes(hostname);
}

function isSameOriginOrNone(req) {
  const origin = req.headers.origin;
  if (!origin) return true;  // curl, the menu bar app, the board
  try {
    return new URL(origin).host === String(req.headers.host || "");
  } catch {
    return false;
  }
}

function hostnameOfThisMac() {
  try {
    return osHostname();
  } catch {
    return "";
  }
}

function clientAddress(req) {
  const address = String(req.socket?.remoteAddress || "unknown");
  return address.startsWith("::ffff:") ? address.slice(7) : address;
}

async function readPackageVersion() {
  try {
    const pkg = JSON.parse(await readFile(new URL("../package.json", import.meta.url), "utf8"));
    return String(pkg.version || "");
  } catch {
    return "";
  }
}

async function readJson(req) {
  const chunks = [];
  let bytes = 0;
  for await (const chunk of req) {
    bytes += chunk.length;
    if (bytes > MAX_BODY_BYTES) {
      throw new Error(`request body too large; max ${MAX_BODY_BYTES} bytes`);
    }
    chunks.push(chunk);
  }
  const text = Buffer.concat(chunks).toString("utf8").trim();
  if (!text) return {};
  return JSON.parse(text);
}

function sendJson(res, status, value) {
  res.writeHead(status, { "Content-Type": "application/json; charset=utf-8" });
  res.end(JSON.stringify(value, null, 2));
}

function setCors(res) {
  res.setHeader("Access-Control-Allow-Origin", process.env.PET_BRIDGE_CORS_ORIGIN || "*");
  res.setHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  res.setHeader("Access-Control-Allow-Headers", "content-type,authorization,x-pet-bridge-token");
}

function stringValue(value) {
  return typeof value === "string" ? value : "";
}

function truncate(value, length) {
  const text = String(value);
  return text.length > length ? `${text.slice(0, length - 1)}...` : text;
}

function numberFromEnv(name, fallback) {
  const value = Number(process.env[name]);
  return Number.isFinite(value) && value > 0 ? value : fallback;
}

function numberFromValue(value, fallback) {
  if (value === null || value === undefined || value === "") return fallback;
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}

function isAuthorized(req, url) {
  if (!INBOUND_TOKEN) return true;
  const headerToken = stringValue(req.headers["x-pet-bridge-token"]);
  const auth = stringValue(req.headers.authorization);
  const bearerToken = auth.startsWith("Bearer ") ? auth.slice("Bearer ".length) : "";
  const queryToken = url.searchParams.get("token") || "";
  return [headerToken, bearerToken, queryToken].includes(INBOUND_TOKEN);
}

function isLoopbackHost(host) {
  return ["127.0.0.1", "localhost", "::1"].includes(host);
}

function stripTrailingSlash(value) {
  return String(value || "").replace(/\/+$/, "");
}

function redactRaw(value) {
  if (Array.isArray(value)) return value.map(redactRaw);
  if (!value || typeof value !== "object") return value;

  const output = {};
  for (const [key, nestedValue] of Object.entries(value)) {
    output[key] = shouldRedactKey(key) ? "[redacted]" : redactRaw(nestedValue);
  }
  return output;
}

function shouldRedactKey(key) {
  return /token|secret|password|authorization|api[_-]?key|private[_-]?key/i.test(key);
}
