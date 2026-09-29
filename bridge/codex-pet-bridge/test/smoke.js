import { spawn } from "node:child_process";
import { once } from "node:events";
import http from "node:http";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";

const port = 17367;
const xiaozhiPort = 17368;
const tempRoot = await mkdtemp(join(tmpdir(), "codex-pet-smoke-"));
const xiaozhiRequests = [];
const xiaozhiServer = http.createServer(async (req, res) => {
  const chunks = [];
  for await (const chunk of req) chunks.push(chunk);
  xiaozhiRequests.push({
    method: req.method,
    url: req.url,
    body: JSON.parse(Buffer.concat(chunks).toString("utf8") || "{}")
  });
  res.writeHead(200, { "content-type": "application/json" });
  res.end(JSON.stringify({ code: 0, data: { ok: true } }));
});
const xiaozhiListening = once(xiaozhiServer, "listening");
xiaozhiServer.listen(xiaozhiPort, "127.0.0.1");

const server = spawn(process.execPath, ["./src/bridge-server.js"], {
  env: {
    ...process.env,
    PET_BRIDGE_PORT: String(port),
    PET_BRIDGE_LOG: join(tempRoot, "events.jsonl"),
    PET_BRIDGE_STATE: join(tempRoot, "bridge-state.json"),
    PET_BRIDGE_CLAUDE_PROJECTS: join(tempRoot, "claude-projects"),
    PET_BRIDGE_CODEX_STATE: join(tempRoot, "state.sqlite"),
    PET_BRIDGE_CODEX_CWD: "/smoke-test",
    PET_BRIDGE_CODEX_SESSIONS: join(tempRoot, "sessions"),
    PET_BRIDGE_SINK_TIMEOUT_MS: "80",
    PET_WEBHOOK_URL: "http://127.0.0.1:9/unreachable",
    XIAOZHI_ASSISTANT_URL: `http://127.0.0.1:${xiaozhiPort}`,
    XIAOZHI_SOURCE_PREFIX: "laptop"
  },
  stdio: ["ignore", "pipe", "pipe"]
});

try {
  await xiaozhiListening;
  await waitForServer(port);

  const hook = spawn(process.execPath, ["./src/claude-hook.js"], {
    env: { ...process.env, PET_BRIDGE_URL: `http://127.0.0.1:${port}/events` },
    stdio: ["pipe", "pipe", "pipe"]
  });
  hook.stdin.end(JSON.stringify({ hook_event_name: "Notification", session_id: "abc", cwd: process.cwd(), notification: { message: "Approve edit?" } }));
  const [code] = await once(hook, "exit");
  if (code !== 0) throw new Error(`hook exited ${code}`);

  const response = await fetch(`http://127.0.0.1:${port}/events`);
  const body = await response.json();
  if (!body.ok || body.events.length !== 1) throw new Error("event was not recorded");
  if (body.events[0].status !== "needs-attention") throw new Error("hook status was not mapped");

  const notificationsResponse = await fetch(`http://127.0.0.1:${port}/notifications`);
  const notificationsBody = await notificationsResponse.json();
  if (!notificationsBody.ok || notificationsBody.unreadCount !== 1) throw new Error("notification was not queued");
  if (notificationsBody.notifications[0].title !== "Waiting for you") throw new Error("notification title was not mapped");
  await waitForOutboxDepth(port, 1);

  const deviceResponse = await fetch(`http://127.0.0.1:${port}/esp32/poll`);
  const deviceBody = await deviceResponse.json();
  if (!deviceBody.ok || deviceBody.unread_count !== 1) throw new Error("device poll did not expose unread count");
  if (!deviceBody.notification?.id) throw new Error("device poll did not expose latest notification");
  if (deviceBody.current_status !== "needs-attention") throw new Error("device poll did not expose current status");

  const ackResponse = await fetch(`http://127.0.0.1:${port}/notifications/${deviceBody.notification.id}/ack`, { method: "POST" });
  const ackBody = await ackResponse.json();
  if (!ackBody.ok || !ackBody.notification.read) throw new Error("notification was not acknowledged");

  await fetch(`http://127.0.0.1:${port}/events`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({
      source: "codex",
      task: "laptop-codex-runtime",
      status: "completed",
      message: "Codex turn ended",
      notify: true
    })
  });

  await waitForXiaozhiRequests(3);
  const [attention, clear, done] = xiaozhiRequests.map((item) => item.body);
  if (attention.source !== "laptop-claude" || attention.status !== "waiting_user" || !attention.needs_user) {
    throw new Error("Claude attention event was not forwarded to XiaoZhi correctly");
  }
  if (clear.status !== "clear" || clear.needs_user !== false) {
    throw new Error("notification ack was not forwarded to XiaoZhi as clear");
  }
  if (done.source !== "laptop-codex" || done.status !== "done" || done.task !== "laptop-codex-runtime" || !done.needs_user) {
    throw new Error("Codex completion event was not forwarded to XiaoZhi correctly");
  }

  await fetch(`http://127.0.0.1:${port}/events`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({
      source: "codex",
      task: "follow-up-current-task",
      status: "thinking",
      message: "Codex is thinking",
      notify: false
    })
  });

  await fetch(`http://127.0.0.1:${port}/events`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({
      source: "codex",
      task: "approve-later",
      status: "needs-attention",
      message: "Need a review",
      notify: true
    })
  });

  await fetch(`http://127.0.0.1:${port}/events`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({
      source: "codex",
      task: "follow-up-current-task",
      status: "thinking",
      message: "Codex is still thinking",
      notify: false
    })
  });

  const staleNotificationResponse = await fetch(`http://127.0.0.1:${port}/esp32/poll`);
  const staleNotificationBody = await staleNotificationResponse.json();
  if (staleNotificationBody.current_status !== "thinking") throw new Error("device poll current status was overridden by stale notification");
  if (staleNotificationBody.source !== "codex") throw new Error("device poll did not expose current source");
  if (staleNotificationBody.task !== "follow-up-current-task") throw new Error("device poll did not expose current task");
  if (staleNotificationBody.notification?.status !== "needs-attention") throw new Error("device poll did not preserve unread notification separately");

  const searchHook = spawn(process.execPath, ["./src/claude-hook.js"], {
    env: { ...process.env, PET_BRIDGE_URL: `http://127.0.0.1:${port}/events` },
    stdio: ["pipe", "pipe", "pipe"]
  });
  searchHook.stdin.end(JSON.stringify({
    hook_event_name: "PreToolUse",
    tool_name: "search_query",
    source: "claude-code",
    task: "find references"
  }));
  const [searchCode] = await once(searchHook, "exit");
  if (searchCode !== 0) throw new Error(`search hook exited ${searchCode}`);

  const searchStateResponse = await fetch(`http://127.0.0.1:${port}/esp32/poll`);
  const searchStateBody = await searchStateResponse.json();
  if (searchStateBody.current_status !== "searching") throw new Error("search tool hook did not expose searching status");

  const toolHook = spawn(process.execPath, ["./src/claude-hook.js"], {
    env: { ...process.env, PET_BRIDGE_URL: `http://127.0.0.1:${port}/events` },
    stdio: ["pipe", "pipe", "pipe"]
  });
  toolHook.stdin.end(JSON.stringify({
    hook_event_name: "PreToolUse",
    tool_name: "apply_patch",
    source: "claude-code",
    task: "edit file"
  }));
  const [toolCode] = await once(toolHook, "exit");
  if (toolCode !== 0) throw new Error(`tool hook exited ${toolCode}`);

  const toolStateResponse = await fetch(`http://127.0.0.1:${port}/esp32/poll`);
  const toolStateBody = await toolStateResponse.json();
  if (toolStateBody.current_status !== "tool-use") throw new Error("tool hook did not expose tool-use status");

  console.log("smoke ok");
} finally {
  server.kill();
  xiaozhiServer.close();
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

async function waitForXiaozhiRequests(count) {
  for (let index = 0; index < 30; index += 1) {
    if (xiaozhiRequests.length >= count) return;
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`xiaozhi sink received ${xiaozhiRequests.length}, expected ${count}`);
}

async function waitForOutboxDepth(port, minDepth) {
  for (let index = 0; index < 30; index += 1) {
    const response = await fetch(`http://127.0.0.1:${port}/health`);
    const body = await response.json();
    if (body.outboxDepth >= minDepth) return;
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error("failed webhook sink did not enter outbox");
}
